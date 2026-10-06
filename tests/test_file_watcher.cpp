// Cross-process change notification through the settings file: this process
// watches the file (inotify on Linux, FindFirstChangeNotification on Windows,
// kqueue on macOS) while a second process, this executable run again with
// --write / --reset, changes it through its own Store. The watcher must fire
// with the value the other process wrote, and again when it resets the key.
#include "check.h"
#include "proc.h"

#include "broconf/store.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

using namespace broconf;

namespace {

constexpr const char* kPath = "org.bro.desktop.interface";
constexpr const char* kKey = "accent-color";

std::shared_ptr<Schema> make_schema() {
    auto schema = std::make_shared<Schema>(kPath);
    schema->add_color(kKey, Color(0, 0, 0));
    return schema;
}

// Child: one Store on the shared file with no watchers, one change, exit.
int child_main(const std::string& mode, const std::filesystem::path& file) {
    StoreOptions opts;
    opts.user_config_path = file;
    opts.enable_dbus = false;
    opts.enable_file_watcher = false;
    auto store = Store::create(opts);
    store->register_schema(make_schema());
    bool ok = mode == "--write" ? store->set(kPath, kKey, Value::make_color(0x35, 0x84, 0xe4))
                                : store->reset(kPath, kKey);
    return ok ? 0 : 3;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3) return child_main(argv[1], argv[2]);

    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_watch_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);
    auto user_path = tmp_dir / "settings.ini";

    StoreOptions opts;
    opts.user_config_path = user_path;
    opts.enable_dbus = false;  // only the file carries the change
    opts.enable_file_watcher = true;
    auto store = Store::create(opts);
    store->register_schema(make_schema());

    std::mutex m;
    std::vector<Color> seen;
    store->watch(kPath, kKey, [&](const std::string&, const std::string&, const Value& v) {
        std::lock_guard lock(m);
        if (v.is_color()) seen.push_back(v.get_color());
    });
    auto saw = [&](const Color& c) {
        return bstest::wait_until([&] {
            std::lock_guard lock(m);
            for (const auto& s : seen) if (s == c) return true;
            return false;
        }, std::chrono::seconds(5));
    };

    // 1. Another process sets the key.
    int rc = bstest::run_process(argv[0], {"--write", user_path.string()});
    REQUIRE(rc == 0);
    CHECK(saw(Color(0x35, 0x84, 0xe4)));
    CHECK(store->get_as<Color>(kPath, kKey) == Color(0x35, 0x84, 0xe4));
    CHECK(!store->is_default(kPath, kKey));

    // 2. Another process resets it: the effective value is the schema default again.
    rc = bstest::run_process(argv[0], {"--reset", user_path.string()});
    REQUIRE(rc == 0);
    CHECK(saw(Color(0, 0, 0)));
    CHECK(store->get_as<Color>(kPath, kKey) == Color(0, 0, 0));
    CHECK(store->is_default(kPath, kKey));

    store.reset();
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    return bstest::finish("test_file_watcher");
}
