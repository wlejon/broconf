#include "broconf/store.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#if defined(__linux__)
#include <systemd/sd-bus.h>
#endif

using namespace broconf;

int main() {
#if !defined(__linux__)
    std::cout << "[SKIP] D-Bus test is Linux-only\n";
    return 77;
#else
    sd_bus* test_bus = nullptr;
    int r = sd_bus_default_user(&test_bus);
    if (r < 0 || !test_bus) {
        std::cout << "[SKIP] D-Bus user session bus unavailable\n";
        return 77;
    }
    sd_bus_flush_close_unref(test_bus);

    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_dbus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);

    auto path_a = tmp_dir / "user_a.ini";
    auto path_b = tmp_dir / "user_b.ini";

    StoreOptions opts_a;
    opts_a.user_config_path = path_a;
    opts_a.enable_dbus = true;
    opts_a.enable_file_watcher = false;

    StoreOptions opts_b;
    opts_b.user_config_path = path_b;
    opts_b.enable_dbus = true;
    opts_b.enable_file_watcher = false;

    auto store_a = Store::create(opts_a);
    auto store_b = Store::create(opts_b);

    // Register schema on both stores
    auto schema = std::make_shared<Schema>("org.bro.desktop.interface");
    schema->add_color("accent-color", Color(0, 0, 0));
    store_a->register_schema(schema);
    store_b->register_schema(schema);

    std::mutex cv_m;
    std::condition_variable cv;
    bool received = false;
    std::string received_path;
    std::string received_key;

    store_b->watch("org.bro.desktop.interface", "accent-color",
                   [&](const std::string& path, const std::string& key, const Value& /*val*/) {
                       std::lock_guard lock(cv_m);
                       received = true;
                       received_path = path;
                       received_key = key;
                       cv.notify_all();
                   });

    // Allow D-Bus match rules to register on session bus
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Store A changes setting
    assert(store_a->set("org.bro.desktop.interface", "accent-color", Value::make_color(255, 0, 128)));

    // Wait for Store B to receive signal
    {
        std::unique_lock lock(cv_m);
        bool res = cv.wait_for(lock, std::chrono::seconds(4), [&] { return received; });
        assert(res && "Timed out waiting for D-Bus signal on Store B");
        assert(received_path == "org.bro.desktop.interface");
        assert(received_key == "accent-color");
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_dbus_sync\n";
    return 0;
#endif
}
