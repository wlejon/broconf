#pragma once

#include "broconf/schema.h"
#include "broconf/storage.h"
#include "broconf/types.h"
#include "broconf/value.h"
#include "broconf/watcher.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace broconf {

struct StoreOptions {
    std::filesystem::path user_config_path;
    std::vector<std::filesystem::path> system_config_paths;
    bool enable_dbus{true};
    bool enable_file_watcher{true};
    std::shared_ptr<SchemaRegistry> schema_registry;
    std::shared_ptr<INotifier> custom_notifier;
    // set()/reset() change memory at once; the file is written this long
    // after the first unwritten change, one write for the whole burst, off
    // the calling thread. flush() and the Store's destruction write at once.
    std::chrono::milliseconds persist_delay{50};
};

class Store : public std::enable_shared_from_this<Store> {
public:
    explicit Store(StoreOptions options = {});
    ~Store();

    // Disable copy, enable move/pointer semantics
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    [[nodiscard]] static std::filesystem::path default_user_config_path();
    [[nodiscard]] static std::vector<std::filesystem::path> default_system_config_paths();

    [[nodiscard]] static std::shared_ptr<Store> create(StoreOptions options = {});
    [[nodiscard]] static std::shared_ptr<Store> default_store();

    // Key operations
    [[nodiscard]] Value get(const std::string& path, const std::string& key) const;
    [[nodiscard]] std::optional<Value> get_optional(const std::string& path, const std::string& key) const;

    template <typename T>
    [[nodiscard]] T get_as(const std::string& path, const std::string& key) const {
        return get(path, key).get<T>();
    }

    bool set(const std::string& path, const std::string& key, const Value& value);
    bool reset(const std::string& path, const std::string& key);

    [[nodiscard]] bool is_default(const std::string& path, const std::string& key) const;
    [[nodiscard]] bool has(const std::string& path, const std::string& key) const;
    [[nodiscard]] std::vector<std::string> list_keys(const std::string& path) const;

    // Change listeners
    WatcherToken watch(const std::string& path, ChangeCallback callback);
    WatcherToken watch(const std::string& path, const std::string& key, ChangeCallback callback);
    bool unwatch(WatcherToken token);

    // Schema registry
    void register_schema(std::shared_ptr<Schema> schema);
    [[nodiscard]] std::shared_ptr<const Schema> find_schema(const std::string& path) const;

    // Explicit reload / sync
    void reload();

    // Writes every change made before the call to the user file and returns
    // once it is there (false: the write failed; the changes stay pending).
    // set()/reset() return before their write; call this where the file must
    // be current, e.g. before another process or Store reads it. The
    // destructor flushes too.
    bool flush();

    [[nodiscard]] const StoreOptions& options() const noexcept { return options_; }

    // Internal hook for external notifications
    void on_external_change(const std::string& path, const std::string& key);

private:
    StoreOptions options_;
    std::shared_ptr<SchemaRegistry> schema_registry_;
    std::unique_ptr<LayeredStorage> storage_;
    WatcherRegistry watcher_registry_;
    std::shared_ptr<INotifier> notifier_;

    void init_notifier();
};

} // namespace broconf
