#pragma once

#include "broconf/watcher.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace broconf {

// macOS: other processes' changes arrive through the settings file. The
// directory holding it is watched with kqueue (EVFILT_VNODE: an atomic rename
// into the directory writes it), and any change makes the Store reload and
// diff it. notify_changed() has nothing to send: the rename is the notification.
class MacNotifier : public INotifier {
public:
    explicit MacNotifier(std::filesystem::path watch_file);
    ~MacNotifier() override;

    void start() override;
    void stop() override;
    void notify_changed(const std::string& path, const std::string& key) override;
    void set_external_change_handler(ExternalChangeHandler handler) override;

private:
    std::filesystem::path watch_file_;
    ExternalChangeHandler change_handler_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    int dir_fd_ = -1;
    int kq_ = -1;

    void run_loop();
};

} // namespace broconf
