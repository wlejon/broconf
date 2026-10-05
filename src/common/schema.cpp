#include "broconf/schema.h"

#include <algorithm>

namespace broconf {

bool KeySchema::validate(const Value& val, std::string* error_msg) const {
    if (!val.is_valid()) {
        if (error_msg) *error_msg = "Value for key '" + name + "' is invalid/null";
        return false;
    }

    // Type matching
    bool type_ok = false;
    switch (type) {
        case Type::Bool:
            type_ok = val.is_bool();
            break;
        case Type::Int64:
            type_ok = val.is_int();
            break;
        case Type::Double:
            type_ok = val.is_double() || val.is_int();
            break;
        case Type::String:
            type_ok = val.is_string();
            break;
        case Type::StringList:
            type_ok = val.is_string_list();
            break;
        case Type::Enum:
            type_ok = val.is_enum() || val.is_string();
            break;
        case Type::Color:
            type_ok = val.is_color();
            break;
        case Type::Rect:
            type_ok = val.is_rect();
            break;
        case Type::Dictionary:
            type_ok = val.is_dictionary();
            break;
    }

    if (!type_ok) {
        if (error_msg) {
            *error_msg = "Type mismatch for key '" + name + "': expected " +
                         to_string(type) + ", got " + to_string(val.type());
        }
        return false;
    }

    // Range checks
    if (type == Type::Int64) {
        int64_t v = val.get_int();
        if (min_value && min_value->is_int() && v < min_value->get_int()) {
            if (error_msg) {
                *error_msg = "Value " + std::to_string(v) + " for key '" + name +
                             "' is less than minimum " + std::to_string(min_value->get_int());
            }
            return false;
        }
        if (max_value && max_value->is_int() && v > max_value->get_int()) {
            if (error_msg) {
                *error_msg = "Value " + std::to_string(v) + " for key '" + name +
                             "' is greater than maximum " + std::to_string(max_value->get_int());
            }
            return false;
        }
    } else if (type == Type::Double) {
        double v = val.get_double();
        if (min_value && (min_value->is_double() || min_value->is_int()) && v < min_value->get_double()) {
            if (error_msg) {
                *error_msg = "Value " + std::to_string(v) + " for key '" + name +
                             "' is less than minimum " + std::to_string(min_value->get_double());
            }
            return false;
        }
        if (max_value && (max_value->is_double() || max_value->is_int()) && v > max_value->get_double()) {
            if (error_msg) {
                *error_msg = "Value " + std::to_string(v) + " for key '" + name +
                             "' is greater than maximum " + std::to_string(max_value->get_double());
            }
            return false;
        }
    }

    // Enum allowed values check
    if (!allowed_enum_values.empty() && (type == Type::Enum || type == Type::String)) {
        std::string enum_str = val.is_enum() ? val.get_enum().name : val.get_string();
        bool found = false;
        for (const auto& allowed : allowed_enum_values) {
            if (allowed == enum_str) {
                found = true;
                break;
            }
        }
        if (!found) {
            if (error_msg) {
                *error_msg = "Value '" + enum_str + "' for enum key '" + name +
                             "' is not one of the allowed values";
            }
            return false;
        }
    }

    // Custom validator
    if (custom_validator) {
        if (!custom_validator(val)) {
            if (error_msg) {
                *error_msg = "Custom validation failed for key '" + name + "'";
            }
            return false;
        }
    }

    return true;
}

// --- Schema Implementation ---

Schema::Schema(std::string id, std::string path)
    : id_(std::move(id)), path_(std::move(path)) {
    if (path_.empty()) {
        path_ = "/" + id_;
        std::replace(path_.begin(), path_.end(), '.', '/');
        if (path_.back() != '/') {
            path_.push_back('/');
        }
    }
}

bool Schema::has_key(const std::string& key) const {
    return keys_.find(key) != keys_.end();
}

const KeySchema* Schema::get_key(const std::string& key) const {
    auto it = keys_.find(key);
    if (it != keys_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> Schema::key_names() const {
    std::vector<std::string> names;
    names.reserve(keys_.size());
    for (const auto& [name, _] : keys_) {
        names.push_back(name);
    }
    return names;
}

bool Schema::validate(const std::string& key, const Value& val, std::string* error_msg) const {
    auto it = keys_.find(key);
    if (it == keys_.end()) {
        if (error_msg) *error_msg = "Key '" + key + "' not defined in schema '" + id_ + "'";
        return false;
    }
    return it->second.validate(val, error_msg);
}

Schema& Schema::add_key(KeySchema key) {
    std::string key_name = key.name;
    keys_[std::move(key_name)] = std::move(key);
    return *this;
}

Schema& Schema::add_bool(std::string name, bool default_val,
                         std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Bool;
    ks.default_value = Value(default_val);
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_int(std::string name, int64_t default_val,
                        std::optional<int64_t> min,
                        std::optional<int64_t> max,
                        std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Int64;
    ks.default_value = Value(default_val);
    if (min) ks.min_value = Value(*min);
    if (max) ks.max_value = Value(*max);
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_double(std::string name, double default_val,
                           std::optional<double> min,
                           std::optional<double> max,
                           std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Double;
    ks.default_value = Value(default_val);
    if (min) ks.min_value = Value(*min);
    if (max) ks.max_value = Value(*max);
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_string(std::string name, std::string default_val,
                           std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::String;
    ks.default_value = Value(std::move(default_val));
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_string_list(std::string name, std::vector<std::string> default_val,
                                std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::StringList;
    ks.default_value = Value(std::move(default_val));
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_enum(std::string name, std::string default_val,
                         std::vector<std::string> allowed_values,
                         std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Enum;
    ks.default_value = Value::make_enum(std::move(default_val));
    ks.allowed_enum_values = std::move(allowed_values);
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_color(std::string name, Color default_val,
                          std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Color;
    ks.default_value = Value(default_val);
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_rect(std::string name, Rect default_val,
                         std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Rect;
    ks.default_value = Value(default_val);
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

Schema& Schema::add_dictionary(std::string name, Dictionary default_val,
                              std::string summary, std::string description) {
    KeySchema ks;
    ks.name = std::move(name);
    ks.type = Type::Dictionary;
    ks.default_value = Value(std::move(default_val));
    ks.summary = std::move(summary);
    ks.description = std::move(description);
    return add_key(std::move(ks));
}

// --- SchemaRegistry Implementation ---

void SchemaRegistry::register_schema(std::shared_ptr<Schema> schema) {
    if (!schema) return;
    std::unique_lock lock(mutex_);
    by_id_[schema->id()] = schema;
    by_path_[schema->path()] = schema;
}

std::shared_ptr<const Schema> SchemaRegistry::find_schema(const std::string& id_or_path) const {
    std::shared_lock lock(mutex_);
    auto it_id = by_id_.find(id_or_path);
    if (it_id != by_id_.end()) {
        return it_id->second;
    }
    auto it_path = by_path_.find(id_or_path);
    if (it_path != by_path_.end()) {
        return it_path->second;
    }
    // Also try normalising trailing slash
    if (!id_or_path.empty() && id_or_path.back() == '/') {
        std::string trimmed = id_or_path.substr(0, id_or_path.size() - 1);
        auto it = by_path_.find(trimmed);
        if (it != by_path_.end()) return it->second;
    } else {
        std::string with_slash = id_or_path + "/";
        auto it = by_path_.find(with_slash);
        if (it != by_path_.end()) return it->second;
    }
    return nullptr;
}

std::vector<std::string> SchemaRegistry::list_schemas() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(by_id_.size());
    for (const auto& [id, _] : by_id_) {
        ids.push_back(id);
    }
    return ids;
}

void SchemaRegistry::clear() {
    std::unique_lock lock(mutex_);
    by_id_.clear();
    by_path_.clear();
}

SchemaRegistry& SchemaRegistry::global() {
    static SchemaRegistry instance;
    return instance;
}

} // namespace broconf
