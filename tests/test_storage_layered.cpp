#include "broconf/storage.h"

#include "check.h"

#include <atomic>
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
    REQUIRE(fs.has_value());
    REQUIRE(fs->get_int() == 14);
    REQUIRE(storage.is_user_set("org.bro.desktop.interface", "font-size"));

    // theme is only in system defaults -> DefaultTheme
    auto th = storage.get("org.bro.desktop.interface", "theme", Type::String);
    REQUIRE(th.has_value());
    REQUIRE(th->get_string() == "DefaultTheme");
    REQUIRE(!storage.is_user_set("org.bro.desktop.interface", "theme"));

    // User overrides theme
    REQUIRE(storage.set("org.bro.desktop.interface", "theme", Value("DarkTheme")));
    REQUIRE(storage.is_user_set("org.bro.desktop.interface", "theme"));
    REQUIRE(storage.get("org.bro.desktop.interface", "theme", Type::String)->get_string() == "DarkTheme");

    // Reset theme -> should fall back to system DefaultTheme
    REQUIRE(storage.reset("org.bro.desktop.interface", "theme"));
    REQUIRE(!storage.is_user_set("org.bro.desktop.interface", "theme"));
    REQUIRE(storage.get("org.bro.desktop.interface", "theme", Type::String)->get_string() == "DefaultTheme");

    // External change reload
    KeyFile ext_kf;
    ext_kf.load_file(user_path);
    ext_kf.set_value("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(16)));
    ext_kf.save_file_atomic(user_path);

    auto changes = storage.reload();
    REQUIRE(!changes.empty());
    bool found_fs_change = false;
    for (const auto& chg : changes) {
        if (chg.section == "org.bro.desktop.interface" && chg.key == "font-size") {
            found_fs_change = true;
            REQUIRE(chg.new_value.has_value());
            REQUIRE(chg.new_value->get_int() == 16);
        }
    }
    REQUIRE(found_fs_change);

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

    std::atomic<int> bad_reads{0};
    std::atomic<int> failed_writes{0};

    auto reader = [&]() {
        while (!start_flag) {}
        while (!done_flag) {
            auto val = storage.get("sec", "key", Type::Int64);
            if (val && (val->get_int() < 0 || val->get_int() > 49)) ++bad_reads;
        }
    };

    auto writer = [&]() {
        while (!start_flag) {}
        for (int i = 0; i < 50; ++i) {
            if (!storage.set("sec", "key", Value(static_cast<int64_t>(i)))) ++failed_writes;
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

    CHECK_EQ(bad_reads.load(), 0);
    CHECK_EQ(failed_writes.load(), 0);

    // Both writers end on 49, and every write went through to disk.
    auto final_val = storage.get("sec", "key", Type::Int64);
    REQUIRE(final_val.has_value());
    CHECK_EQ(final_val->get_int(), static_cast<int64_t>(49));
    CHECK(storage.flush());
    LayeredStorage reread(user_path);
    auto on_disk = reread.get("sec", "key", Type::Int64);
    REQUIRE(on_disk.has_value());
    CHECK_EQ(on_disk->get_int(), static_cast<int64_t>(49));

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_concurrent_read_write\n";
}

int main() {
    test_layered_storage();
    test_concurrent_read_write();
    return bstest::finish("test_storage_layered");
}
