#include "broconf/storage.h"

#include <mutex>
#include <set>

namespace broconf {

LayeredStorage::LayeredStorage(std::filesystem::path user_path,
                               std::vector<std::filesystem::path> system_paths)
    : user_path_(std::move(user_path)), system_paths_(std::move(system_paths)) {
    load_all_locked();
}

void LayeredStorage::load_all_locked() {
    user_file_.load_file(user_path_);
    system_files_.clear();
    system_files_.reserve(system_paths_.size());
    for (const auto& path : system_paths_) {
        KeyFile kf;
        kf.load_file(path);
        system_files_.push_back(std::move(kf));
    }
}

std::optional<Value> LayeredStorage::get(const std::string& section,
                                         const std::string& key,
                                         std::optional<Type> expected_type) const {
    std::shared_lock lock(mutex_);

    // 1. User overrides take precedence
    if (user_file_.has_key(section, key)) {
        auto val = user_file_.get_value(section, key, expected_type);
        if (val) return val;
    }

    // 2. System defaults
    for (const auto& sys_file : system_files_) {
        if (sys_file.has_key(section, key)) {
            auto val = sys_file.get_value(section, key, expected_type);
            if (val) return val;
        }
    }

    return std::nullopt;
}

bool LayeredStorage::is_user_set(const std::string& section, const std::string& key) const {
    std::shared_lock lock(mutex_);
    return user_file_.has_key(section, key);
}

bool LayeredStorage::has(const std::string& section, const std::string& key) const {
    std::shared_lock lock(mutex_);
    if (user_file_.has_key(section, key)) return true;
    for (const auto& sys : system_files_) {
        if (sys.has_key(section, key)) return true;
    }
    return false;
}

std::vector<std::string> LayeredStorage::list_keys(const std::string& section) const {
    std::shared_lock lock(mutex_);
    return list_keys_locked(section);
}

std::vector<std::string> LayeredStorage::list_keys_locked(const std::string& section) const {
    std::set<std::string> key_set;
    for (const auto& k : user_file_.keys(section)) {
        key_set.insert(k);
    }
    for (const auto& sys : system_files_) {
        for (const auto& k : sys.keys(section)) {
            key_set.insert(k);
        }
    }
    return std::vector<std::string>(key_set.begin(), key_set.end());
}

std::vector<std::string> LayeredStorage::list_sections() const {
    std::shared_lock lock(mutex_);
    return list_sections_locked();
}

std::vector<std::string> LayeredStorage::list_sections_locked() const {
    std::set<std::string> sec_set;
    for (const auto& s : user_file_.sections()) {
        sec_set.insert(s);
    }
    for (const auto& sys : system_files_) {
        for (const auto& s : sys.sections()) {
            sec_set.insert(s);
        }
    }
    return std::vector<std::string>(sec_set.begin(), sec_set.end());
}

bool LayeredStorage::set(const std::string& section, const std::string& key, const Value& value) {
    std::unique_lock lock(mutex_);
    user_file_.set_value(section, key, value);
    return user_file_.save_file_atomic(user_path_);
}

bool LayeredStorage::reset(const std::string& section, const std::string& key) {
    std::unique_lock lock(mutex_);
    if (!user_file_.has_key(section, key)) {
        return true; // Already not set by user
    }
    user_file_.remove_key(section, key);
    return user_file_.save_file_atomic(user_path_);
}

std::vector<StorageChange> LayeredStorage::reload() {
    std::unique_lock lock(mutex_);

    // Capture old effective state
    std::map<std::pair<std::string, std::string>, std::optional<Value>> old_state;
    for (const auto& sec : list_sections_locked()) {
        for (const auto& key : list_keys_locked(sec)) {
            old_state[{sec, key}] = user_file_.has_key(sec, key)
                ? user_file_.get_value(sec, key)
                : std::nullopt;
            if (!old_state[{sec, key}]) {
                for (const auto& sys : system_files_) {
                    if (sys.has_key(sec, key)) {
                        old_state[{sec, key}] = sys.get_value(sec, key);
                        break;
                    }
                }
            }
        }
    }

    // Reload files
    load_all_locked();

    // Capture new state and calculate differences
    std::vector<StorageChange> changes;
    std::set<std::pair<std::string, std::string>> all_keys;

    for (const auto& [k, _] : old_state) {
        all_keys.insert(k);
    }
    for (const auto& sec : list_sections_locked()) {
        for (const auto& key : list_keys_locked(sec)) {
            all_keys.insert({sec, key});
        }
    }

    for (const auto& [sec, key] : all_keys) {
        std::optional<Value> new_val = user_file_.has_key(sec, key)
            ? user_file_.get_value(sec, key)
            : std::nullopt;
        if (!new_val) {
            for (const auto& sys : system_files_) {
                if (sys.has_key(sec, key)) {
                    new_val = sys.get_value(sec, key);
                    break;
                }
            }
        }

        std::optional<Value> old_val;
        auto it = old_state.find({sec, key});
        if (it != old_state.end()) {
            old_val = it->second;
        }

        if (old_val != new_val) {
            changes.push_back(StorageChange{sec, key, old_val, new_val});
        }
    }

    return changes;
}

void LayeredStorage::flush() {
    std::shared_lock lock(mutex_);
    user_file_.save_file_atomic(user_path_);
}

} // namespace broconf
