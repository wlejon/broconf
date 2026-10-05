#pragma once

#include "broconf/types.h"
#include "broconf/value.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace broconf {

struct KeySchema {
    std::string name;
    Type type{Type::String};
    Value default_value;
    std::string summary;
    std::string description;
    std::optional<Value> min_value;
    std::optional<Value> max_value;
    std::vector<std::string> allowed_enum_values;
    std::function<bool(const Value&)> custom_validator;

    [[nodiscard]] bool validate(const Value& val, std::string* error_msg = nullptr) const;
};

class Schema {
public:
    explicit Schema(std::string id, std::string path = "");

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    [[nodiscard]] bool has_key(const std::string& key) const;
    [[nodiscard]] const KeySchema* get_key(const std::string& key) const;
    [[nodiscard]] std::vector<std::string> key_names() const;

    [[nodiscard]] bool validate(const std::string& key, const Value& val, std::string* error_msg = nullptr) const;

    Schema& add_key(KeySchema key);

    Schema& add_bool(std::string name, bool default_val,
                     std::string summary = "", std::string description = "");
    Schema& add_int(std::string name, int64_t default_val,
                    std::optional<int64_t> min = std::nullopt,
                    std::optional<int64_t> max = std::nullopt,
                    std::string summary = "", std::string description = "");
    Schema& add_double(std::string name, double default_val,
                       std::optional<double> min = std::nullopt,
                       std::optional<double> max = std::nullopt,
                       std::string summary = "", std::string description = "");
    Schema& add_string(std::string name, std::string default_val,
                       std::string summary = "", std::string description = "");
    Schema& add_string_list(std::string name, std::vector<std::string> default_val,
                            std::string summary = "", std::string description = "");
    Schema& add_enum(std::string name, std::string default_val,
                     std::vector<std::string> allowed_values,
                     std::string summary = "", std::string description = "");
    Schema& add_color(std::string name, Color default_val,
                      std::string summary = "", std::string description = "");
    Schema& add_rect(std::string name, Rect default_val,
                     std::string summary = "", std::string description = "");
    Schema& add_dictionary(std::string name, Dictionary default_val = {},
                           std::string summary = "", std::string description = "");

private:
    std::string id_;
    std::string path_;
    std::unordered_map<std::string, KeySchema> keys_;
};

class SchemaRegistry {
public:
    SchemaRegistry() = default;

    void register_schema(std::shared_ptr<Schema> schema);
    [[nodiscard]] std::shared_ptr<const Schema> find_schema(const std::string& id_or_path) const;
    [[nodiscard]] std::vector<std::string> list_schemas() const;
    void clear();

    static SchemaRegistry& global();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Schema>> by_id_;
    std::unordered_map<std::string, std::shared_ptr<Schema>> by_path_;
};

} // namespace broconf
