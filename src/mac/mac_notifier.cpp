#include "mac_notifier.h"

#include <chrono>

#if defined(__APPLE__)
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace broconf {

MacNotifier::MacNotifier(std::filesystem::path watch_file)
    : watch_file_(std::move(watch_file)) {}

MacNotifier::~MacNotifier() {
    stop();
}

void MacNotifier::set_external_change_handler(ExternalChangeHandler handler) {
    change_handler_ = std::move(handler);
}

void MacNotifier::notify_changed(const std::string& /*path*/, const std::string& /*key*/) {
    // On macOS, distributed notifications or FSEvents/kqueue broadcast updates
}

void MacNotifier::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::thread(&MacNotifier::run_loop, this);
}

void MacNotifier::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void MacNotifier::run_loop() {
#if defined(__APPLE__)
    auto dir = watch_file_.parent_path();
    if (dir.empty()) dir = ".";

    int fd = open(dir.c_str(), O_RDONLY);
    if (fd < 0) return;

    int kq = kqueue();
    if (kq < 0) {
        close(fd);
        return;
    }

    struct kevent change;
    EV_SET(&change, fd, EVFILT_VNODE,
           EV_ADD | EV_ENABLE | EV_CLEAR,
           NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME,
           0, 0);

    kevent(kq, &change, 1, nullptr, 0, nullptr);

    struct timespec timeout { 0, 250000000 }; // 250ms
    struct kevent event;

    while (running_) {
        int nev = kevent(kq, nullptr, 0, &event, 1, &timeout);
        if (nev > 0) {
            if (change_handler_) {
                change_handler_("", "");
            }
        }
    }

    close(kq);
    close(fd);
#else
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
#endif
}

} // namespace broconf
