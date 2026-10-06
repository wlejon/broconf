#include "win_notifier.h"

#include <windows.h>

namespace broconf {

WinNotifier::WinNotifier(std::filesystem::path watch_file)
    : watch_file_(std::move(watch_file)) {}

WinNotifier::~WinNotifier() {
    stop();
}

void WinNotifier::set_external_change_handler(ExternalChangeHandler handler) {
    change_handler_ = std::move(handler);
}

void WinNotifier::notify_changed(const std::string& /*path*/, const std::string& /*key*/) {
    // The file replacement is what other processes' watchers see.
}

void WinNotifier::start() {
    if (running_.exchange(true)) {
        return;
    }

    // The watch is in place before start() returns, so no change made after
    // the Store exists can slip past it. The directory may not exist yet on a
    // first run; create it rather than silently watching nothing.
    auto dir = watch_file_.parent_path();
    if (dir.empty()) dir = ".";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    HANDLE change = FindFirstChangeNotificationW(
        dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (change == INVALID_HANDLE_VALUE) {
        running_ = false;
        return;
    }
    change_handle_ = change;
    stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) {
        FindCloseChangeNotification(change);
        change_handle_ = nullptr;
        running_ = false;
        return;
    }
    worker_thread_ = std::thread(&WinNotifier::run_loop, this);
}

void WinNotifier::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (stop_event_) SetEvent(static_cast<HANDLE>(stop_event_));
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (change_handle_) {
        FindCloseChangeNotification(static_cast<HANDLE>(change_handle_));
        change_handle_ = nullptr;
    }
    if (stop_event_) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
}

void WinNotifier::run_loop() {
    HANDLE handles[2] = {static_cast<HANDLE>(stop_event_), static_cast<HANDLE>(change_handle_)};
    while (running_) {
        DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (r == WAIT_OBJECT_0 + 1) {
            // A change recorded while the handler runs re-signals the handle
            // at FindNextChangeNotification, so a burst is never lost.
            if (change_handler_) change_handler_("", "");
            if (!FindNextChangeNotification(handles[1])) break;
        } else {
            break;  // stop requested, or the wait failed
        }
    }
}

} // namespace broconf
