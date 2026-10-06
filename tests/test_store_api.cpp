#include "broconf/store.h"

#include "check.h"
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace broconf;

void test_store_comprehensive() {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_store_api_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);
    auto user_path = tmp_dir / "user.ini";

    StoreOptions opts;
    opts.user_config_path = user_path;
    opts.enable_dbus = false;
    opts.enable_file_watcher = false;

    auto store = Store::create(opts);

    // 1. Register schemas
    auto iface = std::make_shared<Schema>("org.bro.desktop.interface");
    iface->add_bool("dark-mode", false)
         .add_int("font-size", 11, 8, 32)
         .add_double("ui-scale", 1.0, 0.5, 3.0)
         .add_string("theme", "BroLight")
         .add_string_list("pinned-apps", {"org.bro.term", "org.bro.files"})
         .add_enum("clock-format", "24h", {"12h", "24h"})
         .add_color("accent-color", Color(53, 132, 228))
         .add_rect("window-geometry", Rect(0, 0, 1024, 768))
         .add_dictionary("custom-settings");

    store->register_schema(iface);

    // 2. Test schema defaults
    REQUIRE(store->has("org.bro.desktop.interface", "dark-mode"));
    REQUIRE(store->is_default("org.bro.desktop.interface", "dark-mode"));
    REQUIRE(store->get_as<bool>("org.bro.desktop.interface", "dark-mode") == false);
    REQUIRE(store->get_as<int64_t>("org.bro.desktop.interface", "font-size") == 11);
    REQUIRE(store->get_as<double>("org.bro.desktop.interface", "ui-scale") == 1.0);
    REQUIRE(store->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroLight");
    REQUIRE(store->get_as<Color>("org.bro.desktop.interface", "accent-color") == Color(53, 132, 228));
    REQUIRE(store->get_as<Rect>("org.bro.desktop.interface", "window-geometry") == Rect(0, 0, 1024, 768));

    // 3. Local watch notifications
    bool notified_theme = false;
    std::string new_theme_val;
    auto token = store->watch("org.bro.desktop.interface", "theme",
                 [&](const std::string& /*path*/, const std::string& /*key*/, const Value& val) {
                     notified_theme = true;
                     new_theme_val = val.get_string();
                 });

    // 4. Set valid values
    REQUIRE(store->set("org.bro.desktop.interface", "theme", Value("BroDark")));
    REQUIRE(!store->is_default("org.bro.desktop.interface", "theme"));
    REQUIRE(store->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroDark");
    REQUIRE(notified_theme);
    REQUIRE(new_theme_val == "BroDark");

    // 5. Validation failures
    bool threw = false;
    try {
        // Out of range (min is 8, max is 32)
        store->set("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(40)));
    } catch (const ValidationError&) {
        threw = true;
    }
    REQUIRE(threw);

    threw = false;
    try {
        // Invalid enum
        store->set("org.bro.desktop.interface", "clock-format", Value("solar"));
    } catch (const ValidationError&) {
        threw = true;
    }
    REQUIRE(threw);

    // 6. Reset value
    notified_theme = false;
    REQUIRE(store->reset("org.bro.desktop.interface", "theme"));
    REQUIRE(store->is_default("org.bro.desktop.interface", "theme"));
    REQUIRE(store->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroLight");
    REQUIRE(notified_theme);
    REQUIRE(new_theme_val == "BroLight");

    // 7. Unwatch
    REQUIRE(store->unwatch(token));
    notified_theme = false;
    store->set("org.bro.desktop.interface", "theme", Value("BroSolarized"));
    REQUIRE(!notified_theme); // Was unwatched

    // 8. Persistence check: Re-instantiate Store pointing to same file
    {
        auto store2 = Store::create(opts);
        store2->register_schema(iface);
        REQUIRE(store2->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroSolarized");
        REQUIRE(!store2->is_default("org.bro.desktop.interface", "theme"));
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_store_comprehensive\n";
}

int main() {
    test_store_comprehensive();
    return bstest::finish("test_store_api");
}
