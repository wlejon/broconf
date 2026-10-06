#pragma once

#include "broconf/watcher.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace broconf {

// Windows: other processes' changes arrive through the settings file. The
// directory holding it is watched (FindFirstChangeNotificationW: file names
// and last-write times), and any change makes the Store reload and diff it.
// There is no broadcast channel, so notify_changed() has nothing to send: the
// atomic rename of the file is the notification.
class WinNotifier : public INotifier {
public:
    explicit WinNotifier(std::filesystem::path watch_file);
    ~WinNotifier() override;

    void start() override;
    void stop() override;
    void notify_changed(const std::string& path, const std::string& key) override;
    void set_external_change_handler(ExternalChangeHandler handler) override;

private:
    std::filesystem::path watch_file_;
    ExternalChangeHandler change_handler_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    void* change_handle_ = nullptr;  // HANDLE from FindFirstChangeNotificationW
    void* stop_event_ = nullptr;     // HANDLE, manual-reset event

    void run_loop();
};

} // namespace broconf
