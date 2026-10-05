#pragma once

#include "broconf/types.h"
#include "broconf/value.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace broconf {

class KeyFile {
public:
    KeyFile() = default;

    void load_string(std::string_view content);
    [[nodiscard]] std::string save_string() const;

    bool load_file(const std::filesystem::path& path);
    bool save_file_atomic(const std::filesystem::path& path) const;

    [[nodiscard]] bool has_section(const std::string& section) const;
    [[nodiscard]] bool has_key(const std::string& section, const std::string& key) const;

    [[nodiscard]] std::optional<std::string> get_raw(const std::string& section, const std::string& key) const;
    [[nodiscard]] std::optional<Value> get_value(const std::string& section,
                                                 const std::string& key,
                                                 std::optional<Type> expected_type = std::nullopt) const;

    void set_raw(std::string section, std::string key, std::string value);
    void set_value(const std::string& section, const std::string& key, const Value& value);

    bool remove_key(const std::string& section, const std::string& key);
    bool remove_section(const std::string& section);
    void clear();

    [[nodiscard]] std::vector<std::string> sections() const;
    [[nodiscard]] std::vector<std::string> keys(const std::string& section) const;

    [[nodiscard]] const std::map<std::string, std::map<std::string, std::string>>& raw_data() const noexcept {
        return sections_;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

struct StorageChange {
    std::string section;
    std::string key;
    std::optional<Value> old_value;
    std::optional<Value> new_value;
};

class LayeredStorage {
public:
    explicit LayeredStorage(std::filesystem::path user_path,
                            std::vector<std::filesystem::path> system_paths = {});

    [[nodiscard]] const std::filesystem::path& user_path() const noexcept { return user_path_; }
    [[nodiscard]] const std::vector<std::filesystem::path>& system_paths() const noexcept { return system_paths_; }

    [[nodiscard]] std::optional<Value> get(const std::string& section,
                                           const std::string& key,
                                           std::optional<Type> expected_type = std::nullopt) const;

    [[nodiscard]] bool is_user_set(const std::string& section, const std::string& key) const;
    [[nodiscard]] bool has(const std::string& section, const std::string& key) const;
    [[nodiscard]] std::vector<std::string> list_keys(const std::string& section) const;
    [[nodiscard]] std::vector<std::string> list_sections() const;

    bool set(const std::string& section, const std::string& key, const Value& value);
    bool reset(const std::string& section, const std::string& key);

    std::vector<StorageChange> reload();
    void flush();

private:
    mutable std::shared_mutex mutex_;
    std::filesystem::path user_path_;
    std::vector<std::filesystem::path> system_paths_;

    KeyFile user_file_;
    std::vector<KeyFile> system_files_;

    void load_all_locked();
    [[nodiscard]] std::vector<std::string> list_keys_locked(const std::string& section) const;
    [[nodiscard]] std::vector<std::string> list_sections_locked() const;
};

} // namespace broconf
