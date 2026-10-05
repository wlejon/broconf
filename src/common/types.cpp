#include "broconf/types.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <sstream>

namespace broconf {

std::optional<Type> type_from_string(std::string_view str) noexcept {
    if (str == "bool" || str == "boolean") return Type::Bool;
    if (str == "int" || str == "int64" || str == "integer") return Type::Int64;
    if (str == "double" || str == "float" || str == "number") return Type::Double;
    if (str == "string" || str == "str") return Type::String;
    if (str == "string_list" || str == "strv" || str == "list") return Type::StringList;
    if (str == "enum") return Type::Enum;
    if (str == "color") return Type::Color;
    if (str == "rect") return Type::Rect;
    if (str == "dictionary" || str == "dict" || str == "map") return Type::Dictionary;
    return std::nullopt;
}

std::string Color::to_hex_string(bool include_alpha) const {
    char buf[16];
    if (include_alpha && a != 255) {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", r, g, b, a);
    } else {
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    }
    return std::string(buf);
}

static uint8_t hex_char_to_byte(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0;
}

static bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::optional<Color> Color::from_hex_string(std::string_view hex) noexcept {
    while (!hex.empty() && std::isspace(static_cast<unsigned char>(hex.front()))) {
        hex.remove_prefix(1);
    }
    while (!hex.empty() && std::isspace(static_cast<unsigned char>(hex.back()))) {
        hex.remove_suffix(1);
    }

    if (hex.starts_with('#')) {
        hex.remove_prefix(1);
    }

    for (char c : hex) {
        if (!is_hex_char(c)) return std::nullopt;
    }

    if (hex.size() == 3) { // #RGB -> #RRGGBB
        uint8_t r_part = hex_char_to_byte(hex[0]);
        uint8_t g_part = hex_char_to_byte(hex[1]);
        uint8_t b_part = hex_char_to_byte(hex[2]);
        return Color{
            static_cast<uint8_t>((r_part << 4) | r_part),
            static_cast<uint8_t>((g_part << 4) | g_part),
            static_cast<uint8_t>((b_part << 4) | b_part),
            255
        };
    }

    if (hex.size() == 4) { // #RGBA -> #RRGGBBAA
        uint8_t r_part = hex_char_to_byte(hex[0]);
        uint8_t g_part = hex_char_to_byte(hex[1]);
        uint8_t b_part = hex_char_to_byte(hex[2]);
        uint8_t a_part = hex_char_to_byte(hex[3]);
        return Color{
            static_cast<uint8_t>((r_part << 4) | r_part),
            static_cast<uint8_t>((g_part << 4) | g_part),
            static_cast<uint8_t>((b_part << 4) | b_part),
            static_cast<uint8_t>((a_part << 4) | a_part)
        };
    }

    if (hex.size() == 6) { // #RRGGBB
        uint8_t red = static_cast<uint8_t>((hex_char_to_byte(hex[0]) << 4) | hex_char_to_byte(hex[1]));
        uint8_t grn = static_cast<uint8_t>((hex_char_to_byte(hex[2]) << 4) | hex_char_to_byte(hex[3]));
        uint8_t blu = static_cast<uint8_t>((hex_char_to_byte(hex[4]) << 4) | hex_char_to_byte(hex[5]));
        return Color{red, grn, blu, 255};
    }

    if (hex.size() == 8) { // #RRGGBBAA
        uint8_t red = static_cast<uint8_t>((hex_char_to_byte(hex[0]) << 4) | hex_char_to_byte(hex[1]));
        uint8_t grn = static_cast<uint8_t>((hex_char_to_byte(hex[2]) << 4) | hex_char_to_byte(hex[3]));
        uint8_t blu = static_cast<uint8_t>((hex_char_to_byte(hex[4]) << 4) | hex_char_to_byte(hex[5]));
        uint8_t alp = static_cast<uint8_t>((hex_char_to_byte(hex[6]) << 4) | hex_char_to_byte(hex[7]));
        return Color{red, grn, blu, alp};
    }

    return std::nullopt;
}

std::string Rect::to_string() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d, %d, %d, %d", x, y, width, height);
    return std::string(buf);
}

std::optional<Rect> Rect::from_string(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }

    if (s.starts_with('[') && s.ends_with(']')) {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }

    int32_t values[4] = {0, 0, 0, 0};
    size_t val_idx = 0;

    size_t start = 0;
    while (start < s.size() && val_idx < 4) {
        while (start < s.size() && (std::isspace(static_cast<unsigned char>(s[start])) || s[start] == ',')) {
            start++;
        }
        if (start >= s.size()) break;

        size_t end = start;
        while (end < s.size() && s[end] != ',' && !std::isspace(static_cast<unsigned char>(s[end]))) {
            end++;
        }

        std::string_view part = s.substr(start, end - start);
        int32_t parsed_val = 0;
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), parsed_val);
        if (ec != std::errc() || ptr != part.data() + part.size()) {
            return std::nullopt;
        }

        values[val_idx++] = parsed_val;
        start = end;
    }

    if (val_idx != 4) return std::nullopt;
    return Rect{values[0], values[1], values[2], values[3]};
}

} // namespace broconf
