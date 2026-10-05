#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace broconf {

enum class Type : uint8_t {
    Bool,
    Int64,
    Double,
    String,
    StringList,
    Enum,
    Color,
    Rect,
    Dictionary
};

[[nodiscard]] constexpr const char* to_string(Type type) noexcept {
    switch (type) {
        case Type::Bool: return "bool";
        case Type::Int64: return "int64";
        case Type::Double: return "double";
        case Type::String: return "string";
        case Type::StringList: return "string_list";
        case Type::Enum: return "enum";
        case Type::Color: return "color";
        case Type::Rect: return "rect";
        case Type::Dictionary: return "dictionary";
    }
    return "unknown";
}

[[nodiscard]] std::optional<Type> type_from_string(std::string_view str) noexcept;

struct Color {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr Color() noexcept = default;
    constexpr Color(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 255) noexcept
        : r(red), g(green), b(blue), a(alpha) {}

    constexpr bool operator==(const Color& other) const noexcept = default;

    [[nodiscard]] std::string to_hex_string(bool include_alpha = true) const;
    [[nodiscard]] static std::optional<Color> from_hex_string(std::string_view hex) noexcept;
};

struct Rect {
    int32_t x{0};
    int32_t y{0};
    int32_t width{0};
    int32_t height{0};

    constexpr Rect() noexcept = default;
    constexpr Rect(int32_t x_in, int32_t y_in, int32_t width_in, int32_t height_in) noexcept
        : x(x_in), y(y_in), width(width_in), height(height_in) {}

    constexpr bool operator==(const Rect& other) const noexcept = default;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] static std::optional<Rect> from_string(std::string_view s) noexcept;
};

struct EnumValue {
    std::string name;
    int64_t value{0};

    EnumValue() = default;
    EnumValue(std::string n, int64_t v = 0) : name(std::move(n)), value(v) {}

    bool operator==(const EnumValue& other) const = default;
};

class ConfError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class SchemaError : public ConfError {
public:
    using ConfError::ConfError;
};

class ValidationError : public ConfError {
public:
    using ConfError::ConfError;
};

class StorageError : public ConfError {
public:
    using ConfError::ConfError;
};

class TypeError : public ConfError {
public:
    using ConfError::ConfError;
};

} // namespace broconf
