#include "broconf/store.h"

#include <cassert>
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
    assert(store->has("org.bro.desktop.interface", "dark-mode"));
    assert(store->is_default("org.bro.desktop.interface", "dark-mode"));
    assert(store->get_as<bool>("org.bro.desktop.interface", "dark-mode") == false);
    assert(store->get_as<int64_t>("org.bro.desktop.interface", "font-size") == 11);
    assert(store->get_as<double>("org.bro.desktop.interface", "ui-scale") == 1.0);
    assert(store->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroLight");
    assert(store->get_as<Color>("org.bro.desktop.interface", "accent-color") == Color(53, 132, 228));
    assert(store->get_as<Rect>("org.bro.desktop.interface", "window-geometry") == Rect(0, 0, 1024, 768));

    // 3. Local watch notifications
    bool notified_theme = false;
    std::string new_theme_val;
    auto token = store->watch("org.bro.desktop.interface", "theme",
                 [&](const std::string& /*path*/, const std::string& /*key*/, const Value& val) {
                     notified_theme = true;
                     new_theme_val = val.get_string();
                 });

    // 4. Set valid values
    assert(store->set("org.bro.desktop.interface", "theme", Value("BroDark")));
    assert(!store->is_default("org.bro.desktop.interface", "theme"));
    assert(store->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroDark");
    assert(notified_theme);
    assert(new_theme_val == "BroDark");

    // 5. Validation failures
    bool threw = false;
    try {
        // Out of range (min is 8, max is 32)
        store->set("org.bro.desktop.interface", "font-size", Value(static_cast<int64_t>(40)));
    } catch (const ValidationError&) {
        threw = true;
    }
    assert(threw);

    threw = false;
    try {
        // Invalid enum
        store->set("org.bro.desktop.interface", "clock-format", Value("solar"));
    } catch (const ValidationError&) {
        threw = true;
    }
    assert(threw);

    // 6. Reset value
    notified_theme = false;
    assert(store->reset("org.bro.desktop.interface", "theme"));
    assert(store->is_default("org.bro.desktop.interface", "theme"));
    assert(store->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroLight");
    assert(notified_theme);
    assert(new_theme_val == "BroLight");

    // 7. Unwatch
    assert(store->unwatch(token));
    notified_theme = false;
    store->set("org.bro.desktop.interface", "theme", Value("BroSolarized"));
    assert(!notified_theme); // Was unwatched

    // 8. Persistence check: Re-instantiate Store pointing to same file
    {
        auto store2 = Store::create(opts);
        store2->register_schema(iface);
        assert(store2->get_as<std::string>("org.bro.desktop.interface", "theme") == "BroSolarized");
        assert(!store2->is_default("org.bro.desktop.interface", "theme"));
    }

    std::filesystem::remove_all(tmp_dir);
    std::cout << "[PASS] test_store_comprehensive\n";
}

int main() {
    test_store_comprehensive();
    std::cout << "All store API tests passed successfully.\n";
    return 0;
}
