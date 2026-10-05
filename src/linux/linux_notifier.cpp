#include "linux_notifier.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

namespace broconf {

namespace {

static int on_signal_changed(sd_bus_message* m, void* userdata, sd_bus_error* /*ret_error*/) {
    auto* self = static_cast<LinuxNotifier*>(userdata);
    if (!self) return 0;

    const char* sender = sd_bus_message_get_sender(m);
    if (sender && !self->unique_bus_name().empty() && self->unique_bus_name() == sender) {
        return 0; // Ignore self-sent broadcast signal
    }

    const char* path = nullptr;
    const char* key = nullptr;
    int r = sd_bus_message_read(m, "ss", &path, &key);
    if (r >= 0 && path && key) {
        self->handle_remote_signal(path, key);
    }
    return 0;
}

} // namespace

LinuxNotifier::LinuxNotifier(std::filesystem::path watch_file,
                             bool enable_dbus,
                             bool enable_inotify)
    : watch_file_(std::move(watch_file)),
      enable_dbus_(enable_dbus),
      enable_inotify_(enable_inotify) {}

LinuxNotifier::~LinuxNotifier() {
    stop();
}

void LinuxNotifier::set_external_change_handler(ExternalChangeHandler handler) {
    change_handler_ = std::move(handler);
}

void LinuxNotifier::handle_remote_signal(const std::string& path, const std::string& key) {
    if (change_handler_) {
        change_handler_(path, key);
    }
}

void LinuxNotifier::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }

    stop_event_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);

    if (enable_inotify_) {
        setup_inotify();
    }

    if (enable_dbus_) {
        setup_dbus();
    }

    worker_thread_ = std::thread(&LinuxNotifier::run_loop, this);
}

void LinuxNotifier::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (stop_event_fd_ >= 0) {
        uint64_t val = 1;
        (void)write(stop_event_fd_, &val, sizeof(val));
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    cleanup_dbus();
    cleanup_inotify();

    if (stop_event_fd_ >= 0) {
        close(stop_event_fd_);
        stop_event_fd_ = -1;
    }
}

void LinuxNotifier::setup_inotify() {
    inotify_fd_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (inotify_fd_ < 0) return;

    auto dir = watch_file_.parent_path();
    std::error_code ec;
    if (!dir.empty() && !std::filesystem::exists(dir, ec)) {
        std::filesystem::create_directories(dir, ec);
    }

    std::string watch_dir = dir.empty() ? "." : dir.string();
    watch_wd_ = inotify_add_watch(inotify_fd_, watch_dir.c_str(),
                                  IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
}

void LinuxNotifier::cleanup_inotify() {
    if (inotify_fd_ >= 0) {
        if (watch_wd_ >= 0) {
            inotify_rm_watch(inotify_fd_, watch_wd_);
            watch_wd_ = -1;
        }
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
}

void LinuxNotifier::setup_dbus() {
    std::lock_guard lock(bus_mutex_);
    int r = sd_bus_open_user(&bus_);
    if (r < 0 || !bus_) {
        // Session bus unavailable or headless container
        bus_ = nullptr;
        return;
    }

    const char* unique = nullptr;
    if (sd_bus_get_unique_name(bus_, &unique) >= 0 && unique) {
        unique_bus_name_ = unique;
    }

    // Add match for Changed signal on org.bro.Config
    r = sd_bus_match_signal(bus_,
                            &match_slot_,
                            nullptr, // sender
                            "/org/bro/Config",
                            "org.bro.Config",
                            "Changed",
                            on_signal_changed,
                            this);
    if (r < 0) {
        sd_bus_slot_unref(match_slot_);
        match_slot_ = nullptr;
    }
}

void LinuxNotifier::cleanup_dbus() {
    std::lock_guard lock(bus_mutex_);
    if (match_slot_) {
        sd_bus_slot_unref(match_slot_);
        match_slot_ = nullptr;
    }
    if (bus_) {
        sd_bus_flush_close_unref(bus_);
        bus_ = nullptr;
    }
}

void LinuxNotifier::notify_changed(const std::string& path, const std::string& key) {
    {
        std::lock_guard lock(bus_mutex_);
        if (bus_) {
            sd_bus_emit_signal(bus_,
                               "/org/bro/Config",
                               "org.bro.Config",
                               "Changed",
                               "ss",
                               path.c_str(),
                               key.c_str());
            sd_bus_flush(bus_);
        }
    }
    if (stop_event_fd_ >= 0) {
        uint64_t val = 1;
        (void)write(stop_event_fd_, &val, sizeof(val));
    }
}

void LinuxNotifier::handle_inotify_events() {
    alignas(struct inotify_event) char buffer[4096];
    std::string target_filename = watch_file_.filename().string();
    bool matched = false;

    while (true) {
        ssize_t len = read(inotify_fd_, buffer, sizeof(buffer));
        if (len <= 0) {
            break;
        }

        ssize_t i = 0;
        while (i < len) {
            auto* event = reinterpret_cast<struct inotify_event*>(&buffer[i]);
            if (event->len > 0) {
                if (target_filename == event->name) {
                    matched = true;
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
    }

    if (matched && change_handler_) {
        change_handler_("", "");
    }
}

void LinuxNotifier::run_loop() {
    while (running_) {
        {
            std::lock_guard lock(bus_mutex_);
            if (bus_) {
                int r;
                while ((r = sd_bus_process(bus_, nullptr)) > 0) {}
                if (r < 0) {
                    // bus error
                }
            }
        }

        struct pollfd fds[3];
        int nfds = 0;

        fds[nfds].fd = stop_event_fd_;
        fds[nfds].events = POLLIN;
        int stop_idx = nfds++;

        int inotify_idx = -1;
        if (inotify_fd_ >= 0) {
            fds[nfds].fd = inotify_fd_;
            fds[nfds].events = POLLIN;
            inotify_idx = nfds++;
        }

        int bus_idx = -1;
        int timeout_ms = 250;
        {
            std::lock_guard lock(bus_mutex_);
            if (bus_) {
                int bus_fd = sd_bus_get_fd(bus_);
                if (bus_fd >= 0) {
                    int bus_events = sd_bus_get_events(bus_);
                    fds[nfds].fd = bus_fd;
                    fds[nfds].events = static_cast<short>(bus_events);
                    bus_idx = nfds++;

                    uint64_t bus_timeout_us = 0;
                    if (sd_bus_get_timeout(bus_, &bus_timeout_us) >= 0) {
                        if (bus_timeout_us != UINT64_MAX) {
                            auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
                            if (bus_timeout_us <= static_cast<uint64_t>(now_us)) {
                                timeout_ms = 0;
                            } else {
                                timeout_ms = static_cast<int>((bus_timeout_us - now_us) / 1000);
                                if (timeout_ms > 250) timeout_ms = 250;
                            }
                        }
                    }
                }
            }
        }

        int ret = poll(fds, nfds, timeout_ms);
        if (ret <= 0) continue;

        if (fds[stop_idx].revents & POLLIN) {
            uint64_t val = 0;
            (void)read(stop_event_fd_, &val, sizeof(val));
            if (!running_) {
                break;
            }
        }

        if (inotify_idx >= 0 && (fds[inotify_idx].revents & POLLIN)) {
            handle_inotify_events();
        }

        if (bus_idx >= 0 && (fds[bus_idx].revents != 0)) {
            // Processed on next loop iteration under bus_mutex_
        }
    }
}

} // namespace broconf
