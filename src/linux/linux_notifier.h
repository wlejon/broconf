#pragma once

#include "broconf/watcher.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

typedef struct sd_bus sd_bus;
typedef struct sd_bus_slot sd_bus_slot;

namespace broconf {

class LinuxNotifier : public INotifier {
public:
    explicit LinuxNotifier(std::filesystem::path watch_file,
                           bool enable_dbus = true,
                           bool enable_inotify = true);
    ~LinuxNotifier() override;

    void start() override;
    void stop() override;
    void notify_changed(const std::string& path, const std::string& key) override;
    void set_external_change_handler(ExternalChangeHandler handler) override;

    // Internal callbacks
    void handle_remote_signal(const std::string& path, const std::string& key);
    [[nodiscard]] const std::string& unique_bus_name() const noexcept { return unique_bus_name_; }

private:
    std::filesystem::path watch_file_;
    bool enable_dbus_{true};
    bool enable_inotify_{true};

    ExternalChangeHandler change_handler_;
    std::atomic<bool> running_{false};
    std::thread worker_thread_;

    int stop_event_fd_{-1};
    int inotify_fd_{-1};
    int watch_wd_{-1};

    sd_bus* bus_{nullptr};
    sd_bus_slot* match_slot_{nullptr};
    std::string unique_bus_name_;
    mutable std::mutex bus_mutex_;

    void run_loop();
    void handle_inotify_events();
    void setup_inotify();
    void cleanup_inotify();
    void setup_dbus();
    void cleanup_dbus();
};

} // namespace broconf
