#include "broconf/store.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

using namespace broconf;

int main() {
#if !defined(__linux__)
    std::cout << "[SKIP] Inotify test is Linux-only\n";
    return 77;
#else
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_inotify_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);
    auto user_path = tmp_dir / "settings.ini";

    StoreOptions opts;
    opts.user_config_path = user_path;
    opts.system_config_paths = {};
    opts.enable_dbus = false;
    opts.enable_file_watcher = true;

    auto store = Store::create(opts);

    auto schema = std::make_shared<Schema>("org.bro.desktop.interface");
    schema->add_color("accent-color", Color(0, 0, 0));
    store->register_schema(schema);

    // Initial state
    store->set("org.bro.desktop.interface", "accent-color", Value::make_color(0, 0, 0));

    std::mutex cv_m;
    std::condition_variable cv;
    bool notified = false;
    Color received_color;

    store->watch("org.bro.desktop.interface", "accent-color",
                 [&](const std::string& /*path*/, const std::string& /*key*/, const Value& new_val) {
                     std::lock_guard lock(cv_m);
                     if (new_val.is_color()) {
                         received_color = new_val.get_color();
                         notified = true;
                         cv.notify_all();
                     }
                 });

    // Allow inotify watcher thread to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Simulate another process writing to the file atomically
    KeyFile external_kf;
    external_kf.set_value("org.bro.desktop.interface", "accent-color",
                          Value::make_color(0x35, 0x84, 0xe4));
    assert(external_kf.save_file_atomic(user_path));

    // Wait for inotify event to trigger reload
    {
        std::unique_lock lock(cv_m);
        bool res = cv.wait_for(lock, std::chrono::seconds(3), [&] { return notified; });
        assert(res && "Timed out waiting for inotify event");
        assert(received_color == Color(0x35, 0x84, 0xe4, 255));
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_inotify_watcher\n";
    return 0;
#endif
}
