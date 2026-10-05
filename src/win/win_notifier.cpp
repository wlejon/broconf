#include "win_notifier.h"

#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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
    // On Windows, filesystem changes or named pipes broadcast updates
}

void WinNotifier::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::thread(&WinNotifier::run_loop, this);
}

void WinNotifier::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void WinNotifier::run_loop() {
#if defined(_WIN32)
    auto dir = watch_file_.parent_path();
    if (dir.empty()) dir = ".";

    std::wstring wdir = dir.wstring();
    HANDLE hChange = FindFirstChangeNotificationW(
        wdir.c_str(),
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE
    );

    if (hChange == INVALID_HANDLE_VALUE) {
        return;
    }

    while (running_) {
        DWORD waitStatus = WaitForSingleObject(hChange, 250);
        if (waitStatus == WAIT_OBJECT_0) {
            if (change_handler_) {
                change_handler_("", "");
            }
            FindNextChangeNotification(hChange);
        }
    }

    FindCloseChangeNotification(hChange);
#else
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
#endif
}

} // namespace broconf
