#pragma once

#include "broconf/watcher.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace broconf {

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

    void run_loop();
};

} // namespace broconf
