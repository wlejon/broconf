#include "broconf/storage.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

using namespace broconf;

void test_layered_storage() {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_layered_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);

    auto sys_path = tmp_dir / "system_settings.ini";
    auto user_path = tmp_dir / "user_settings.ini";

    // Setup system defaults
    KeyFile sys_kf;
    sys_kf.set_value("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(10)));
    sys_kf.set_value("org.bro.desktop.interface", "theme", Value("DefaultTheme"));
    sys_kf.save_file_atomic(sys_path);

    // Setup user overrides
    KeyFile user_kf;
    user_kf.set_value("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(14)));
    user_kf.save_file_atomic(user_path);

    LayeredStorage storage(user_path, {sys_path});

    // font-size is in user override -> 14
    auto fs = storage.get("org.bro.desktop.interface", "font-size", Type::Int64);
    assert(fs.has_value());
    assert(fs->get_int() == 14);
    assert(storage.is_user_set("org.bro.desktop.interface", "font-size"));

    // theme is only in system defaults -> DefaultTheme
    auto th = storage.get("org.bro.desktop.interface", "theme", Type::String);
    assert(th.has_value());
    assert(th->get_string() == "DefaultTheme");
    assert(!storage.is_user_set("org.bro.desktop.interface", "theme"));

    // User overrides theme
    assert(storage.set("org.bro.desktop.interface", "theme", Value("DarkTheme")));
    assert(storage.is_user_set("org.bro.desktop.interface", "theme"));
    assert(storage.get("org.bro.desktop.interface", "theme", Type::String)->get_string() == "DarkTheme");

    // Reset theme -> should fall back to system DefaultTheme
    assert(storage.reset("org.bro.desktop.interface", "theme"));
    assert(!storage.is_user_set("org.bro.desktop.interface", "theme"));
    assert(storage.get("org.bro.desktop.interface", "theme", Type::String)->get_string() == "DefaultTheme");

    // External change reload
    KeyFile ext_kf;
    ext_kf.load_file(user_path);
    ext_kf.set_value("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(16)));
    ext_kf.save_file_atomic(user_path);

    auto changes = storage.reload();
    assert(!changes.empty());
    bool found_fs_change = false;
    for (const auto& chg : changes) {
        if (chg.section == "org.bro.desktop.interface" && chg.key == "font-size") {
            found_fs_change = true;
            assert(chg.new_value.has_value());
            assert(chg.new_value->get_int() == 16);
        }
    }
    assert(found_fs_change);

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_layered_storage\n";
}

void test_concurrent_read_write() {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_concurrent_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);
    auto user_path = tmp_dir / "user.ini";

    LayeredStorage storage(user_path);

    std::atomic<bool> start_flag{false};
    std::atomic<bool> done_flag{false};

    auto reader = [&]() {
        while (!start_flag) {}
        while (!done_flag) {
            auto val = storage.get("sec", "key", Type::Int64);
            (void)val;
        }
    };

    auto writer = [&]() {
        while (!start_flag) {}
        for (int i = 0; i < 50; ++i) {
            storage.set("sec", "key", Value(static_cast<int64_t>(i)));
            std::this_thread::yield();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back(reader);
    for (int i = 0; i < 2; ++i) threads.emplace_back(writer);

    start_flag = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    done_flag = true;

    for (auto& t : threads) {
        t.join();
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_concurrent_read_write\n";
}

int main() {
    test_layered_storage();
    test_concurrent_read_write();
    std::cout << "All layered storage tests passed successfully.\n";
    return 0;
}
