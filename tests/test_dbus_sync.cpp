// Cross-process change notification over D-Bus (Linux), on a private session
// bus this test starts (dbus-daemon), so it neither needs nor touches the
// user's session. The file watcher is off: only the org.bro.Config.Changed
// signal can tell this process that something changed.
//
//   1. A second process (this executable with --write) sets a key in the
//      shared settings file through its own Store; its signal must make this
//      Store reload and fire the watcher with the new value.
//   2. An independent client, gdbus, emits Changed for a key whose value was
//      rewritten on disk behind the Store's back; the Store must pick it up.
//   3. A signal from this Store's own connection is not echoed back to it.
#include "check.h"
#include "proc.h"

#include "broconf/store.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

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
    schema->add_string("theme", "Light");
    return schema;
}

int child_main(const std::filesystem::path& file) {
    StoreOptions opts;
    opts.user_config_path = file;
    opts.enable_dbus = true;
    opts.enable_file_watcher = false;
    auto store = Store::create(opts);
    store->register_schema(make_schema());
    return store->set(kPath, kKey, Value::make_color(255, 0, 128)) ? 0 : 3;
}

// A private session bus for the test's lifetime.
struct PrivateBus {
    std::string address;
    pid_t pid = -1;

    PrivateBus() {
        FILE* p = popen("dbus-daemon --session --fork --print-address=1 --print-pid=1 2>/dev/null", "r");
        if (!p) return;
        char line[512];
        if (fgets(line, sizeof(line), p)) {
            address = line;
            while (!address.empty() && (address.back() == '\n' || address.back() == '\r')) address.pop_back();
        }
        if (fgets(line, sizeof(line), p)) pid = static_cast<pid_t>(std::atol(line));
        pclose(p);
    }
    ~PrivateBus() {
        if (pid > 0) kill(pid, SIGTERM);
    }
    bool ok() const { return !address.empty() && pid > 0; }
};

bool have_program(const char* name) {
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write") return child_main(argv[2]);

    if (!have_program("dbus-daemon")) {
        bstest::skip("test_dbus_sync", "dbus-daemon is not installed");
    }
    PrivateBus bus;
    if (!bus.ok()) bstest::skip("test_dbus_sync", "could not start a private dbus-daemon");
    setenv("DBUS_SESSION_BUS_ADDRESS", bus.address.c_str(), 1);

    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("broconf_dbus_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tmp_dir);
    auto user_path = tmp_dir / "settings.ini";

    StoreOptions opts;
    opts.user_config_path = user_path;
    opts.enable_dbus = true;
    opts.enable_file_watcher = false;
    auto store = Store::create(opts);
    store->register_schema(make_schema());

    std::mutex m;
    std::vector<std::pair<std::string, std::string>> events;  // key, serialized value
    store->watch(kPath, [&](const std::string&, const std::string& key, const Value& v) {
        std::lock_guard lock(m);
        events.emplace_back(key, v.serialize());
    });
    auto saw = [&](const std::string& key, const std::string& value, std::chrono::milliseconds t) {
        return bstest::wait_until([&] {
            std::lock_guard lock(m);
            for (const auto& [k, v] : events) if (k == key && v == value) return true;
            return false;
        }, t);
    };

    // 1. Another process writes and signals.
    REQUIRE(bstest::run_process(argv[0], {"--write", user_path.string()}) == 0);
    CHECK(saw(kKey, Value::make_color(255, 0, 128).serialize(), std::chrono::seconds(5)));
    CHECK(store->get_as<Color>(kPath, kKey) == Color(255, 0, 128));

    // 2. The file changes behind the Store's back; gdbus announces it.
    if (have_program("gdbus")) {
        KeyFile kf;
        REQUIRE(kf.load_file(user_path));
        kf.set_value(kPath, "theme", Value("Dark"));
        REQUIRE(kf.save_file_atomic(user_path));
        int rc = std::system(("gdbus emit --session --object-path /org/bro/Config "
                              "--signal org.bro.Config.Changed "
                              "'org.bro.desktop.interface' 'theme' >/dev/null"));
        REQUIRE(rc == 0);
        CHECK(saw("theme", Value("Dark").serialize(), std::chrono::seconds(5)));
        CHECK(store->get_as<std::string>(kPath, "theme") == "Dark");
    } else {
        std::printf("Note: gdbus not installed; the independent-emitter check did not run\n");
    }

    // 3. Our own set notifies our watcher once (locally), not a second time
    //    through our own signal coming back over the bus.
    {
        std::lock_guard lock(m);
        events.clear();
    }
    REQUIRE(store->set(kPath, "theme", Value("Solarized")));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    {
        std::lock_guard lock(m);
        int n = 0;
        for (const auto& [k, v] : events) if (k == "theme") ++n;
        CHECK_EQ(n, 1);
    }

    store.reset();
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    return bstest::finish("test_dbus_sync");
}
