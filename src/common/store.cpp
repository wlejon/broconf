#include "broconf/store.h"

#include <algorithm>
#include <cstdlib>
#include <set>

#if defined(__linux__)
#include "../linux/linux_notifier.h"
#elif defined(_WIN32)
#include "../win/win_notifier.h"
#elif defined(__APPLE__)
#include "../mac/mac_notifier.h"
#endif

namespace broconf {

namespace {

std::string normalize_section(std::string_view p) {
    while (!p.empty() && p.front() == '/') p.remove_prefix(1);
    while (!p.empty() && p.back() == '/') p.remove_suffix(1);
    std::string s(p);
    std::replace(s.begin(), s.end(), '/', '.');
    return s;
}

} // namespace

std::filesystem::path Store::default_user_config_path() {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata && *appdata) {
        return std::filesystem::path(appdata) / "bro" / "settings.ini";
    }
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile && *userprofile) {
        return std::filesystem::path(userprofile) / "AppData" / "Roaming" / "bro" / "settings.ini";
    }
    return "settings.ini";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / "Library" / "Preferences" / "bro" / "settings.ini";
    }
    return "settings.ini";
#else
    const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config_home && *xdg_config_home) {
        return std::filesystem::path(xdg_config_home) / "bro" / "settings.ini";
    }
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".config" / "bro" / "settings.ini";
    }
    return "settings.ini";
#endif
}

std::vector<std::filesystem::path> Store::default_system_config_paths() {
    std::vector<std::filesystem::path> paths;
#if defined(_WIN32)
    const char* progdata = std::getenv("PROGRAMDATA");
    if (progdata && *progdata) {
        paths.push_back(std::filesystem::path(progdata) / "bro" / "settings.ini");
    }
#elif defined(__APPLE__)
    paths.push_back("/Library/Preferences/bro/settings.ini");
#else
    const char* xdg_dirs = std::getenv("XDG_CONFIG_DIRS");
    if (xdg_dirs && *xdg_dirs) {
        std::string s(xdg_dirs);
        size_t start = 0;
        while (start < s.size()) {
            size_t end = s.find(':', start);
            if (end == std::string::npos) end = s.size();
            std::string dir = s.substr(start, end - start);
            if (!dir.empty()) {
                paths.push_back(std::filesystem::path(dir) / "bro" / "settings.ini");
            }
            start = end + 1;
        }
    } else {
        paths.push_back("/etc/xdg/bro/settings.ini");
    }
    paths.push_back("/usr/share/bro/settings.ini");
#endif
    return paths;
}

Store::Store(StoreOptions options) : options_(std::move(options)) {
    if (options_.user_config_path.empty()) {
        options_.user_config_path = default_user_config_path();
    }
    if (options_.system_config_paths.empty()) {
        options_.system_config_paths = default_system_config_paths();
    }

    if (options_.schema_registry) {
        schema_registry_ = options_.schema_registry;
    } else {
        schema_registry_ = std::make_shared<SchemaRegistry>();
    }

    storage_ = std::make_unique<LayeredStorage>(options_.user_config_path,
                                                options_.system_config_paths,
                                                options_.persist_delay);

    init_notifier();

    // Other processes hear of a change once it is in the file (a D-Bus
    // signal sent at set() time would have them reload a file without it).
    storage_->set_persisted_handler([this](const std::vector<std::pair<std::string, std::string>>& written) {
        if (!notifier_) return;
        for (const auto& [section, key] : written) {
            notifier_->notify_changed(section, key);
        }
    });
}

Store::~Store() {
    // Write what is pending while the notifier can still announce it.
    storage_->close();
    if (notifier_) {
        notifier_->stop();
    }
}

bool Store::flush() {
    return storage_->flush();
}

void Store::init_notifier() {
    if (options_.custom_notifier) {
        notifier_ = options_.custom_notifier;
    } else {
#if defined(__linux__)
        if (options_.enable_dbus || options_.enable_file_watcher) {
            notifier_ = std::make_shared<LinuxNotifier>(
                options_.user_config_path,
                options_.enable_dbus,
                options_.enable_file_watcher
            );
        }
#elif defined(_WIN32)
        if (options_.enable_file_watcher) {
            notifier_ = std::make_shared<WinNotifier>(options_.user_config_path);
        }
#elif defined(__APPLE__)
        if (options_.enable_file_watcher) {
            notifier_ = std::make_shared<MacNotifier>(options_.user_config_path);
        }
#endif
    }

    if (notifier_) {
        notifier_->set_external_change_handler([this](const std::string& path, const std::string& key) {
            this->on_external_change(path, key);
        });
        notifier_->start();
    }
}

std::shared_ptr<Store> Store::create(StoreOptions options) {
    return std::make_shared<Store>(std::move(options));
}

std::shared_ptr<Store> Store::default_store() {
    static std::shared_ptr<Store> instance = create();
    return instance;
}

Value Store::get(const std::string& path, const std::string& key) const {
    auto val = get_optional(path, key);
    if (val) {
        return *val;
    }
    throw ConfError("Key '" + key + "' not found in configuration for path '" + path + "'");
}

std::optional<Value> Store::get_optional(const std::string& path, const std::string& key) const {
    std::string section = normalize_section(path);

    // 1. Storage (user override or system defaults)
    std::optional<Type> expected_type;
    auto schema = schema_registry_->find_schema(path);
    if (schema) {
        auto key_schema = schema->get_key(key);
        if (key_schema) {
            expected_type = key_schema->type;
        }
    }

    auto val = storage_->get(section, key, expected_type);
    if (val) {
        return val;
    }

    // 2. Schema default
    if (schema) {
        auto key_schema = schema->get_key(key);
        if (key_schema && key_schema->default_value.is_valid()) {
            return key_schema->default_value;
        }
    }

    return std::nullopt;
}

bool Store::set(const std::string& path, const std::string& key, const Value& value) {
    std::string section = normalize_section(path);

    // Schema validation
    auto schema = schema_registry_->find_schema(path);
    if (schema) {
        std::string err;
        if (!schema->validate(key, value, &err)) {
            throw ValidationError(err);
        }
    }

    if (!storage_->set(section, key, value)) {
        return false;
    }

    // Notify local watchers. Other processes hear of it once it is written.
    watcher_registry_.notify(path, key, value);

    return true;
}

bool Store::reset(const std::string& path, const std::string& key) {
    std::string section = normalize_section(path);

    if (!storage_->reset(section, key)) {
        return false;
    }

    auto new_val = get_optional(path, key);
    Value effective_val = new_val.value_or(Value());

    // Notify local watchers. Other processes hear of it once it is written.
    watcher_registry_.notify(path, key, effective_val);

    return true;
}

bool Store::is_default(const std::string& path, const std::string& key) const {
    std::string section = normalize_section(path);
    return !storage_->is_user_set(section, key);
}

bool Store::has(const std::string& path, const std::string& key) const {
    std::string section = normalize_section(path);
    if (storage_->has(section, key)) {
        return true;
    }
    auto schema = schema_registry_->find_schema(path);
    if (schema && schema->has_key(key)) {
        return true;
    }
    return false;
}

std::vector<std::string> Store::list_keys(const std::string& path) const {
    std::string section = normalize_section(path);
    std::set<std::string> keys;

    for (const auto& k : storage_->list_keys(section)) {
        keys.insert(k);
    }

    auto schema = schema_registry_->find_schema(path);
    if (schema) {
        for (const auto& k : schema->key_names()) {
            keys.insert(k);
        }
    }

    return std::vector<std::string>(keys.begin(), keys.end());
}

WatcherToken Store::watch(const std::string& path, ChangeCallback callback) {
    return watcher_registry_.add_watch(path, std::move(callback));
}

WatcherToken Store::watch(const std::string& path, const std::string& key, ChangeCallback callback) {
    return watcher_registry_.add_watch(path, key, std::move(callback));
}

bool Store::unwatch(WatcherToken token) {
    return watcher_registry_.remove_watch(token);
}

void Store::register_schema(std::shared_ptr<Schema> schema) {
    schema_registry_->register_schema(std::move(schema));
}

std::shared_ptr<const Schema> Store::find_schema(const std::string& path) const {
    return schema_registry_->find_schema(path);
}

void Store::reload() {
    auto changes = storage_->reload();
    for (const auto& chg : changes) {
        auto val = get_optional(chg.section, chg.key);
        Value effective_val = val.value_or(Value());
        watcher_registry_.notify(chg.section, chg.key, effective_val);
    }
}

void Store::on_external_change(const std::string& path, const std::string& key) {
    if (path.empty() && key.empty()) {
        reload();
    } else {
        auto changes = storage_->reload();
        if (changes.empty()) {
            // Even if reload didn't see file diff (e.g. D-Bus signal arrived before file write or in-flight),
            // fetch current value and notify watchers
            auto val = get_optional(path, key);
            if (val) {
                watcher_registry_.notify(path, key, *val);
            }
        } else {
            for (const auto& chg : changes) {
                auto val = get_optional(chg.section, chg.key);
                Value effective_val = val.value_or(Value());
                watcher_registry_.notify(chg.section, chg.key, effective_val);
            }
        }
    }
}

} // namespace broconf
