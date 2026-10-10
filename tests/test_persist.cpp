// Deferred, coalesced, atomic persistence: set()/reset() change memory and
// return; a writer thread writes the file later, one write per burst, merged
// onto the file as it is on disk; reload() keeps changes not yet written;
// flush(), close() and destruction write what is pending.
#include "broconf/storage.h"
#include "broconf/store.h"

#include "check.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace broconf;
using namespace std::chrono_literals;

namespace {

std::filesystem::path scratch(const char* tag) {
    auto dir = std::filesystem::temp_directory_path() /
               (std::string("broconf_persist_") + tag + "_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir;
}

std::optional<std::string> on_disk(const std::filesystem::path& file, const std::string& sec, const std::string& key) {
    KeyFile kf;
    kf.load_file(file);
    return kf.get_raw(sec, key);
}

size_t temp_files(const std::filesystem::path& dir) {
    size_t n = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().filename().string().find(".tmp.") != std::string::npos) ++n;
    }
    return n;
}

void test_burst_is_one_write() {
    auto dir = scratch("burst");
    auto file = dir / "user.ini";
    LayeredStorage storage(file, {}, 200ms);
    std::atomic<int> writes{0};
    storage.set_persisted_handler([&](const auto&) { ++writes; });

    for (int i = 0; i < 5; ++i) {
        REQUIRE(storage.set("ui", "volume", Value(static_cast<int64_t>(i))));
        // read-after-set: memory is current at once
        CHECK_EQ(storage.get("ui", "volume", Type::Int64)->get_int(), static_cast<int64_t>(i));
    }
    CHECK(!on_disk(file, "ui", "volume").has_value());  // not yet written
    CHECK(bstest::wait_until([&] { return writes.load() >= 1; }, 5s));
    std::this_thread::sleep_for(300ms);
    CHECK_EQ(writes.load(), 1);  // the burst was one write
    CHECK_EQ(on_disk(file, "ui", "volume").value_or(""), std::string("4"));
    CHECK_EQ(temp_files(dir), static_cast<size_t>(0));

    std::filesystem::remove_all(dir);
    std::cout << "[PASS] test_burst_is_one_write\n";
}

void test_reload_keeps_pending_and_merges() {
    auto dir = scratch("merge");
    auto file = dir / "user.ini";
    // A delay long enough that nothing is written before flush().
    LayeredStorage storage(file, {}, 60s);
    REQUIRE(storage.set("ui", "mine", Value("ours")));

    // Another process writes the file meanwhile.
    KeyFile other;
    other.set_value("ui", "theirs", Value("external"));
    REQUIRE(other.save_file_atomic(file));

    auto changes = storage.reload();
    bool saw_theirs = false, saw_mine = false;
    for (const auto& c : changes) {
        if (c.key == "theirs") saw_theirs = true;
        if (c.key == "mine") saw_mine = true;
    }
    CHECK(saw_theirs);
    CHECK(!saw_mine);  // our unwritten value is not mistaken for a removal
    CHECK_EQ(storage.get("ui", "mine", Type::String)->get_string(), std::string("ours"));
    CHECK_EQ(storage.get("ui", "theirs", Type::String)->get_string(), std::string("external"));

    // A pending reset survives a reload too.
    REQUIRE(storage.set("ui", "gone", Value("x")));
    REQUIRE(storage.flush());
    REQUIRE(storage.reset("ui", "gone"));
    storage.reload();
    CHECK(!storage.is_user_set("ui", "gone"));

    // The write merges onto the file: both keys are there.
    REQUIRE(storage.flush());
    CHECK_EQ(on_disk(file, "ui", "mine").value_or(""), std::string("\"ours\""));
    CHECK_EQ(on_disk(file, "ui", "theirs").value_or(""), std::string("\"external\""));
    CHECK(!on_disk(file, "ui", "gone").has_value());
    CHECK_EQ(temp_files(dir), static_cast<size_t>(0));

    std::filesystem::remove_all(dir);
    std::cout << "[PASS] test_reload_keeps_pending_and_merges\n";
}

void test_destruction_flushes() {
    auto dir = scratch("dtor");
    auto file = dir / "user.ini";
    {
        StoreOptions opts;
        opts.user_config_path = file;
        opts.system_config_paths = {dir / "none.ini"};
        opts.enable_dbus = false;
        opts.enable_file_watcher = false;
        opts.persist_delay = 60s;
        auto store = Store::create(opts);
        REQUIRE(store->set("ui", "last", Value("before-exit")));
        CHECK(!on_disk(file, "ui", "last").has_value());
    }
    CHECK_EQ(on_disk(file, "ui", "last").value_or(""), std::string("\"before-exit\""));

    // After close(), changes are written as they are made.
    {
        LayeredStorage storage(file, {}, 60s);
        storage.close();
        REQUIRE(storage.set("ui", "late", Value("sync")));
        CHECK_EQ(on_disk(file, "ui", "late").value_or(""), std::string("\"sync\""));
    }

    std::filesystem::remove_all(dir);
    std::cout << "[PASS] test_destruction_flushes\n";
}

void test_set_cost_with_large_store() {
    auto dir = scratch("cost");
    auto file = dir / "user.ini";
    LayeredStorage storage(file);
    std::vector<std::string> big;
    for (int i = 0; i < 1500; ++i) big.push_back("D:/Music/Artist " + std::to_string(i) + "/Track.flac");
    REQUIRE(storage.set("library", "paths", Value(big)));
    REQUIRE(storage.flush());

    std::vector<double> ms;
    for (int i = 0; i < 40; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        storage.set("ui", "volume", Value(static_cast<int64_t>(i)));
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    std::cout << "  median set " << ms[ms.size() / 2] << " ms\n";
    CHECK(ms[ms.size() / 2] < 0.5);
    REQUIRE(storage.flush());
    CHECK_EQ(on_disk(file, "ui", "volume").value_or(""), std::string("39"));

    std::filesystem::remove_all(dir);
    std::cout << "[PASS] test_set_cost_with_large_store\n";
}

}  // namespace

int main() {
    test_burst_is_one_write();
    test_reload_keeps_pending_and_merges();
    test_destruction_flushes();
    test_set_cost_with_large_store();
    return bstest::finish("test_persist");
}
