#pragma once

#include "broconf/types.h"

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace broconf {

class Value;

class Dictionary {
public:
    Dictionary();
    ~Dictionary();
    Dictionary(const Dictionary& other);
    Dictionary& operator=(const Dictionary& other);
    Dictionary(Dictionary&& other) noexcept;
    Dictionary& operator=(Dictionary&& other) noexcept;

    explicit Dictionary(std::map<std::string, Value> items);
    Dictionary(std::initializer_list<std::pair<const std::string, Value>> items);

    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool contains(const std::string& key) const;
    [[nodiscard]] std::optional<Value> get(const std::string& key) const;
    [[nodiscard]] const Value* find(const std::string& key) const;

    void set(std::string key, Value value);
    bool remove(const std::string& key);
    void clear();

    [[nodiscard]] const std::map<std::string, Value>& entries() const noexcept;
    [[nodiscard]] std::map<std::string, Value>& entries() noexcept;

    bool operator==(const Dictionary& other) const;
    bool operator!=(const Dictionary& other) const { return !(*this == other); }

private:
    std::shared_ptr<std::map<std::string, Value>> entries_;
};

class Value {
public:
    using VariantType = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        std::vector<std::string>,
        EnumValue,
        Color,
        Rect,
        Dictionary
    >;

    Value() noexcept;
    Value(bool val) noexcept;
    Value(int32_t val) noexcept;
    Value(int64_t val) noexcept;
    Value(double val) noexcept;
    Value(const char* val);
    Value(std::string val);
    Value(std::string_view val);
    Value(std::vector<std::string> val);
    Value(EnumValue val);
    Value(Color val) noexcept;
    Value(Rect val) noexcept;
    Value(Dictionary val);

    ~Value() = default;
    Value(const Value&) = default;
    Value& operator=(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(Value&&) noexcept = default;

    // Factory methods
    [[nodiscard]] static Value make_enum(std::string name, int64_t value = 0);
    [[nodiscard]] static Value make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    [[nodiscard]] static Value make_rect(int32_t x, int32_t y, int32_t width, int32_t height);
    [[nodiscard]] static Value make_dictionary(std::map<std::string, Value> items = {});

    [[nodiscard]] Type type() const;
    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] bool is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
    [[nodiscard]] bool is_int() const noexcept { return std::holds_alternative<int64_t>(data_); }
    [[nodiscard]] bool is_double() const noexcept { return std::holds_alternative<double>(data_); }
    [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
    [[nodiscard]] bool is_string_list() const noexcept { return std::holds_alternative<std::vector<std::string>>(data_); }
    [[nodiscard]] bool is_enum() const noexcept { return std::holds_alternative<EnumValue>(data_); }
    [[nodiscard]] bool is_color() const noexcept { return std::holds_alternative<Color>(data_); }
    [[nodiscard]] bool is_rect() const noexcept { return std::holds_alternative<Rect>(data_); }
    [[nodiscard]] bool is_dictionary() const noexcept { return std::holds_alternative<Dictionary>(data_); }

    [[nodiscard]] bool get_bool() const;
    [[nodiscard]] int64_t get_int() const;
    [[nodiscard]] double get_double() const;
    [[nodiscard]] const std::string& get_string() const;
    [[nodiscard]] const std::vector<std::string>& get_string_list() const;
    [[nodiscard]] const EnumValue& get_enum() const;
    [[nodiscard]] Color get_color() const;
    [[nodiscard]] Rect get_rect() const;
    [[nodiscard]] const Dictionary& get_dictionary() const;

    template <typename T>
    [[nodiscard]] const T* get_if() const noexcept {
        return std::get_if<T>(&data_);
    }

    template <typename T>
    [[nodiscard]] const T& get() const {
        if (auto p = std::get_if<T>(&data_)) {
            return *p;
        }
        throw TypeError("Value type mismatch");
    }

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static Value deserialize(Type expected_type, std::string_view text);
    [[nodiscard]] static Value parse_inferred(std::string_view text);

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

    const VariantType& raw_variant() const noexcept { return data_; }

private:
    VariantType data_;
};

} // namespace broconf
