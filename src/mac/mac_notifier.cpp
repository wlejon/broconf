#include "mac_notifier.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

namespace broconf {

namespace {
constexpr uintptr_t kStopIdent = 1;
}

MacNotifier::MacNotifier(std::filesystem::path watch_file)
    : watch_file_(std::move(watch_file)) {}

MacNotifier::~MacNotifier() {
    stop();
}

void MacNotifier::set_external_change_handler(ExternalChangeHandler handler) {
    change_handler_ = std::move(handler);
}

void MacNotifier::notify_changed(const std::string& /*path*/, const std::string& /*key*/) {
    // The file replacement is what other processes' watchers see.
}

void MacNotifier::start() {
    if (running_.exchange(true)) {
        return;
    }

    // Registered before start() returns, so no later change slips past; the
    // directory is created on a first run rather than silently not watched.
    auto dir = watch_file_.parent_path();
    if (dir.empty()) dir = ".";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    dir_fd_ = open(dir.c_str(), O_RDONLY | O_EVTONLY | O_CLOEXEC);
    kq_ = kqueue();
    if (dir_fd_ < 0 || kq_ < 0) {
        if (dir_fd_ >= 0) close(dir_fd_);
        if (kq_ >= 0) close(kq_);
        dir_fd_ = kq_ = -1;
        running_ = false;
        return;
    }

    struct kevent changes[2];
    EV_SET(&changes[0], dir_fd_, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME, 0, nullptr);
    EV_SET(&changes[1], kStopIdent, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    kevent(kq_, changes, 2, nullptr, 0, nullptr);

    worker_thread_ = std::thread(&MacNotifier::run_loop, this);
}

void MacNotifier::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (kq_ >= 0) {
        struct kevent wake;
        EV_SET(&wake, kStopIdent, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        kevent(kq_, &wake, 1, nullptr, 0, nullptr);
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (kq_ >= 0) close(kq_);
    if (dir_fd_ >= 0) close(dir_fd_);
    kq_ = dir_fd_ = -1;
}

void MacNotifier::run_loop() {
    while (running_) {
        struct kevent event;
        int nev = kevent(kq_, nullptr, 0, &event, 1, nullptr);
        if (nev < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (nev == 0) continue;
        if (event.filter == EVFILT_USER) break;
        if (event.filter == EVFILT_VNODE && change_handler_) {
            change_handler_("", "");
        }
    }
}

} // namespace broconf
