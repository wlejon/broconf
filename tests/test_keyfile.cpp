#include "broconf/storage.h"

#include "check.h"
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace broconf;

void test_keyfile_in_memory() {
    KeyFile kf;
    std::string text = R"(
# System configuration file
[org.bro.desktop.interface]
accent-color = #3584e4
font-size = 11
dark-mode = true
pinned-apps = ["org.bro.term", "org.bro.files"]

; Window manager settings
[org.bro.desktop.wm]
window-rect = 100, 100, 800, 600
focus-follows-mouse = false
)";

    kf.load_string(text);

    REQUIRE(kf.has_section("org.bro.desktop.interface"));
    REQUIRE(kf.has_section("org.bro.desktop.wm"));
    REQUIRE(!kf.has_section("org.bro.nonexistent"));

    REQUIRE(kf.has_key("org.bro.desktop.interface", "accent-color"));
    REQUIRE(kf.has_key("org.bro.desktop.interface", "font-size"));
    REQUIRE(kf.has_key("org.bro.desktop.interface", "dark-mode"));
    REQUIRE(kf.has_key("org.bro.desktop.wm", "window-rect"));

    auto col = kf.get_value("org.bro.desktop.interface", "accent-color", Type::Color);
    REQUIRE(col.has_value());
    REQUIRE(col->get_color() == Color(0x35, 0x84, 0xe4, 255));

    auto font_size = kf.get_value("org.bro.desktop.interface", "font-size", Type::Int64);
    REQUIRE(font_size.has_value());
    REQUIRE(font_size->get_int() == 11);

    auto dark_mode = kf.get_value("org.bro.desktop.interface", "dark-mode", Type::Bool);
    REQUIRE(dark_mode.has_value());
    REQUIRE(dark_mode->get_bool() == true);

    auto rect = kf.get_value("org.bro.desktop.wm", "window-rect", Type::Rect);
    REQUIRE(rect.has_value());
    REQUIRE(rect->get_rect() == Rect(100, 100, 800, 600));

    // Modify
    kf.set_value("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(14)));
    REQUIRE(kf.get_value("org.bro.desktop.interface", "font-size", Type::Int64)->get_int() == 14);

    // Remove key
    REQUIRE(kf.remove_key("org.bro.desktop.wm", "focus-follows-mouse"));
    REQUIRE(!kf.has_key("org.bro.desktop.wm", "focus-follows-mouse"));

    // Save and reload
    std::string saved = kf.save_string();
    KeyFile kf2;
    kf2.load_string(saved);
    REQUIRE(kf2.get_value("org.bro.desktop.interface", "font-size", Type::Int64)->get_int() == 14);

    std::cout << "[PASS] test_keyfile_in_memory\n";
}

void test_keyfile_atomic_file() {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_kf_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);

    auto target_file = tmp_dir / "subdir" / "settings.ini";

    KeyFile kf;
    kf.set_value("section1", "key1", Value("val1"));
    kf.set_value("section1", "num", Value(static_cast<int64_t>(99)));

    // Save atomic
    REQUIRE(kf.save_file_atomic(target_file));
    REQUIRE(std::filesystem::exists(target_file));

    // Load back
    KeyFile loaded;
    REQUIRE(loaded.load_file(target_file));
    REQUIRE(loaded.has_key("section1", "key1"));
    REQUIRE(loaded.get_value("section1", "key1", Type::String)->get_string() == "val1");
    REQUIRE(loaded.get_value("section1", "num", Type::Int64)->get_int() == 99);

    // Update atomically
    loaded.set_value("section1", "key1", Value("updated_val"));
    REQUIRE(loaded.save_file_atomic(target_file));

    KeyFile reloaded;
    REQUIRE(reloaded.load_file(target_file));
    REQUIRE(reloaded.get_value("section1", "key1", Type::String)->get_string() == "updated_val");

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_keyfile_atomic_file\n";
}

int main() {
    test_keyfile_in_memory();
    test_keyfile_atomic_file();
    return bstest::finish("test_keyfile");
}
