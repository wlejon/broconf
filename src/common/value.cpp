#include "broconf/value.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace broconf {

// --- Dictionary Implementation ---

Dictionary::Dictionary() : entries_(std::make_shared<std::map<std::string, Value>>()) {}

Dictionary::~Dictionary() = default;

Dictionary::Dictionary(const Dictionary& other)
    : entries_(std::make_shared<std::map<std::string, Value>>(*other.entries_)) {}

Dictionary& Dictionary::operator=(const Dictionary& other) {
    if (this != &other) {
        entries_ = std::make_shared<std::map<std::string, Value>>(*other.entries_);
    }
    return *this;
}

Dictionary::Dictionary(Dictionary&& other) noexcept = default;
Dictionary& Dictionary::operator=(Dictionary&& other) noexcept = default;

Dictionary::Dictionary(std::map<std::string, Value> items)
    : entries_(std::make_shared<std::map<std::string, Value>>(std::move(items))) {}

Dictionary::Dictionary(std::initializer_list<std::pair<const std::string, Value>> items)
    : entries_(std::make_shared<std::map<std::string, Value>>(items)) {}

size_t Dictionary::size() const noexcept {
    return entries_ ? entries_->size() : 0;
}

bool Dictionary::empty() const noexcept {
    return !entries_ || entries_->empty();
}

bool Dictionary::contains(const std::string& key) const {
    return entries_ && entries_->find(key) != entries_->end();
}

std::optional<Value> Dictionary::get(const std::string& key) const {
    if (!entries_) return std::nullopt;
    auto it = entries_->find(key);
    if (it != entries_->end()) {
        return it->second;
    }
    return std::nullopt;
}

const Value* Dictionary::find(const std::string& key) const {
    if (!entries_) return nullptr;
    auto it = entries_->find(key);
    if (it != entries_->end()) {
        return &it->second;
    }
    return nullptr;
}

void Dictionary::set(std::string key, Value value) {
    if (!entries_) {
        entries_ = std::make_shared<std::map<std::string, Value>>();
    }
    (*entries_)[std::move(key)] = std::move(value);
}

bool Dictionary::remove(const std::string& key) {
    if (!entries_) return false;
    return entries_->erase(key) > 0;
}

void Dictionary::clear() {
    if (entries_) {
        entries_->clear();
    }
}

const std::map<std::string, Value>& Dictionary::entries() const noexcept {
    static const std::map<std::string, Value> empty_map;
    return entries_ ? *entries_ : empty_map;
}

std::map<std::string, Value>& Dictionary::entries() noexcept {
    if (!entries_) {
        entries_ = std::make_shared<std::map<std::string, Value>>();
    }
    return *entries_;
}

bool Dictionary::operator==(const Dictionary& other) const {
    if (entries_ == other.entries_) return true;
    if (!entries_ && other.entries_->empty()) return true;
    if (!other.entries_ && entries_->empty()) return true;
    if (!entries_ || !other.entries_) return false;
    return *entries_ == *other.entries_;
}

// --- Value Constructors ---

Value::Value() noexcept : data_(std::monostate{}) {}
Value::Value(bool val) noexcept : data_(val) {}
Value::Value(int32_t val) noexcept : data_(static_cast<int64_t>(val)) {}
Value::Value(int64_t val) noexcept : data_(val) {}
Value::Value(double val) noexcept : data_(val) {}
Value::Value(const char* val) : data_(std::string(val ? val : "")) {}
Value::Value(std::string val) : data_(std::move(val)) {}
Value::Value(std::string_view val) : data_(std::string(val)) {}
Value::Value(std::vector<std::string> val) : data_(std::move(val)) {}
Value::Value(EnumValue val) : data_(std::move(val)) {}
Value::Value(Color val) noexcept : data_(val) {}
Value::Value(Rect val) noexcept : data_(val) {}
Value::Value(Dictionary val) : data_(std::move(val)) {}

Value Value::make_enum(std::string name, int64_t value) {
    return Value(EnumValue(std::move(name), value));
}

Value Value::make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return Value(Color(r, g, b, a));
}

Value Value::make_rect(int32_t x, int32_t y, int32_t width, int32_t height) {
    return Value(Rect(x, y, width, height));
}

Value Value::make_dictionary(std::map<std::string, Value> items) {
    return Value(Dictionary(std::move(items)));
}

Type Value::type() const {
    if (std::holds_alternative<bool>(data_)) return Type::Bool;
    if (std::holds_alternative<int64_t>(data_)) return Type::Int64;
    if (std::holds_alternative<double>(data_)) return Type::Double;
    if (std::holds_alternative<std::string>(data_)) return Type::String;
    if (std::holds_alternative<std::vector<std::string>>(data_)) return Type::StringList;
    if (std::holds_alternative<EnumValue>(data_)) return Type::Enum;
    if (std::holds_alternative<Color>(data_)) return Type::Color;
    if (std::holds_alternative<Rect>(data_)) return Type::Rect;
    if (std::holds_alternative<Dictionary>(data_)) return Type::Dictionary;
    throw TypeError("Value is uninitialized (monostate)");
}

bool Value::is_valid() const noexcept {
    return !std::holds_alternative<std::monostate>(data_);
}

bool Value::get_bool() const {
    return get<bool>();
}

int64_t Value::get_int() const {
    return get<int64_t>();
}

double Value::get_double() const {
    if (auto p = std::get_if<double>(&data_)) return *p;
    if (auto p = std::get_if<int64_t>(&data_)) return static_cast<double>(*p);
    throw TypeError("Value is not a double");
}

const std::string& Value::get_string() const {
    return get<std::string>();
}

const std::vector<std::string>& Value::get_string_list() const {
    return get<std::vector<std::string>>();
}

const EnumValue& Value::get_enum() const {
    return get<EnumValue>();
}

Color Value::get_color() const {
    return get<Color>();
}

Rect Value::get_rect() const {
    return get<Rect>();
}

const Dictionary& Value::get_dictionary() const {
    return get<Dictionary>();
}

bool Value::operator==(const Value& other) const {
    if (data_.index() != other.data_.index()) {
        // Special case: allow comparison between int64_t and double if equal
        if (std::holds_alternative<int64_t>(data_) && std::holds_alternative<double>(other.data_)) {
            return static_cast<double>(std::get<int64_t>(data_)) == std::get<double>(other.data_);
        }
        if (std::holds_alternative<double>(data_) && std::holds_alternative<int64_t>(other.data_)) {
            return std::get<double>(data_) == static_cast<double>(std::get<int64_t>(other.data_));
        }
        return false;
    }
    return data_ == other.data_;
}

// --- Parsing and Serialization Helpers ---

static void write_escaped_string(std::ostream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b"; break;
            case '\f': os << "\\f"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    os << buf;
                } else {
                    os << c;
                }
                break;
        }
    }
    os << '"';
}

static void serialize_internal(std::ostream& os, const Value& val) {
    if (!val.is_valid()) {
        os << "null";
        return;
    }
    switch (val.type()) {
        case Type::Bool:
            os << (val.get_bool() ? "true" : "false");
            break;
        case Type::Int64:
            os << val.get_int();
            break;
        case Type::Double: {
            double d = val.get_double();
            if (std::isnan(d)) {
                os << "nan";
            } else if (std::isinf(d)) {
                os << (d < 0 ? "-inf" : "inf");
            } else {
                std::ostringstream ss;
                ss << std::setprecision(14) << d;
                std::string s = ss.str();
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
                    s += ".0";
                }
                os << s;
            }
            break;
        }
        case Type::String:
            write_escaped_string(os, val.get_string());
            break;
        case Type::StringList: {
            os << "[";
            const auto& list = val.get_string_list();
            for (size_t i = 0; i < list.size(); ++i) {
                if (i > 0) os << ", ";
                write_escaped_string(os, list[i]);
            }
            os << "]";
            break;
        }
        case Type::Enum:
            write_escaped_string(os, val.get_enum().name);
            break;
        case Type::Color:
            os << '"' << val.get_color().to_hex_string() << '"';
            break;
        case Type::Rect: {
            const auto r = val.get_rect();
            os << "[" << r.x << ", " << r.y << ", " << r.width << ", " << r.height << "]";
            break;
        }
        case Type::Dictionary: {
            os << "{";
            const auto& entries = val.get_dictionary().entries();
            size_t idx = 0;
            for (const auto& [k, v] : entries) {
                if (idx++ > 0) os << ", ";
                write_escaped_string(os, k);
                os << ": ";
                serialize_internal(os, v);
            }
            os << "}";
            break;
        }
    }
}

std::string Value::serialize() const {
    std::ostringstream ss;
    serialize_internal(ss, *this);
    return ss.str();
}

// Simple recursive-descent JSON-style parser for Value
class ValueParser {
public:
    explicit ValueParser(std::string_view text) : text_(text), pos_(0) {}

    Value parse_value() {
        skip_whitespace();
        if (pos_ >= text_.size()) {
            throw TypeError("Unexpected end of input while parsing Value");
        }

        char c = text_[pos_];
        if (c == '"') {
            return Value(parse_string());
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '{') {
            return parse_object();
        }
        if (c == 't' || c == 'T' || c == 'f' || c == 'F') {
            return Value(parse_bool());
        }
        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            return parse_number();
        }
        if (c == '#') {
            return parse_color_raw();
        }
        if (text_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return Value();
        }

        // Unquoted identifier / string fallback
        size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != ']' && text_[pos_] != '}' && !std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            pos_++;
        }
        std::string raw = std::string(text_.substr(start, pos_ - start));
        if (raw == "true" || raw == "yes" || raw == "on") return Value(true);
        if (raw == "false" || raw == "no" || raw == "off") return Value(false);
        return Value(raw);
    }

    std::string parse_string() {
        skip_whitespace();
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            throw TypeError("Expected string literal starting with '\"'");
        }
        pos_++; // skip opening '"'
        std::string result;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return result;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) throw TypeError("Unterminated string escape sequence");
                char esc = text_[pos_++];
                switch (esc) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) throw TypeError("Incomplete \\u hex escape");
                        std::string hex_str = std::string(text_.substr(pos_, 4));
                        pos_ += 4;
                        unsigned int codepoint = 0;
                        std::stringstream ss;
                        ss << std::hex << hex_str;
                        ss >> codepoint;
                        if (codepoint <= 0x7F) {
                            result.push_back(static_cast<char>(codepoint));
                        } else {
                            // Simple UTF-8 encoding
                            if (codepoint <= 0x7FF) {
                                result.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
                                result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                            } else {
                                result.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
                                result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                                result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
                            }
                        }
                        break;
                    }
                    default: result.push_back(esc); break;
                }
            } else {
                result.push_back(c);
            }
        }
        throw TypeError("Unclosed string literal");
    }

private:
    void skip_whitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            pos_++;
        }
    }

    bool parse_bool() {
        if (pos_ + 4 <= text_.size()) {
            std::string s = std::string(text_.substr(pos_, 4));
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (s == "true") {
                pos_ += 4;
                return true;
            }
        }
        if (pos_ + 5 <= text_.size()) {
            std::string s = std::string(text_.substr(pos_, 5));
            for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (s == "false") {
                pos_ += 5;
                return false;
            }
        }
        throw TypeError("Expected boolean literal");
    }

    Value parse_number() {
        size_t start = pos_;
        bool is_float = false;
        if (text_[pos_] == '-' || text_[pos_] == '+') pos_++;
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c >= '0' && c <= '9') {
                pos_++;
            } else if (c == '.' || c == 'e' || c == 'E') {
                is_float = true;
                pos_++;
                if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) pos_++;
            } else {
                break;
            }
        }
        std::string_view num_str = text_.substr(start, pos_ - start);
        if (is_float) {
            double d = 0.0;
            std::istringstream ss{std::string(num_str)};
            ss >> d;
            return Value(d);
        } else {
            int64_t n = 0;
            auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), n);
            if (ec == std::errc()) {
                return Value(n);
            }
            throw TypeError("Failed to parse integer number: " + std::string(num_str));
        }
    }

    Value parse_color_raw() {
        size_t start = pos_;
        while (pos_ < text_.size() && (text_[pos_] == '#' || std::isxdigit(static_cast<unsigned char>(text_[pos_])))) {
            pos_++;
        }
        std::string_view hex = text_.substr(start, pos_ - start);
        auto col = Color::from_hex_string(hex);
        if (col) {
            return Value(*col);
        }
        throw TypeError("Invalid hex color: " + std::string(hex));
    }

    Value parse_array() {
        pos_++; // skip '['
        skip_whitespace();
        std::vector<std::string> str_list;
        std::vector<Value> val_list;
        bool all_strings = true;
        bool all_ints = true;

        if (pos_ < text_.size() && text_[pos_] == ']') {
            pos_++;
            return Value(std::vector<std::string>{});
        }

        while (pos_ < text_.size()) {
            skip_whitespace();
            Value v = parse_value();
            if (v.is_string()) {
                str_list.push_back(v.get_string());
            } else {
                all_strings = false;
            }
            if (!v.is_int()) {
                all_ints = false;
            }
            val_list.push_back(std::move(v));

            skip_whitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                pos_++;
            } else if (pos_ < text_.size() && text_[pos_] == ']') {
                pos_++;
                break;
            } else {
                throw TypeError("Expected ',' or ']' in array");
            }
        }

        if (all_ints && val_list.size() == 4) {
            return Value(Rect{
                static_cast<int32_t>(val_list[0].get_int()),
                static_cast<int32_t>(val_list[1].get_int()),
                static_cast<int32_t>(val_list[2].get_int()),
                static_cast<int32_t>(val_list[3].get_int())
            });
        }
        if (all_strings) {
            return Value(str_list);
        }
        // Fallback: list of string representations
        std::vector<std::string> fallback_strs;
        for (const auto& v : val_list) {
            fallback_strs.push_back(v.serialize());
        }
        return Value(fallback_strs);
    }

    Value parse_object() {
        pos_++; // skip '{'
        skip_whitespace();
        std::map<std::string, Value> map;

        if (pos_ < text_.size() && text_[pos_] == '}') {
            pos_++;
            return Value(Dictionary(std::move(map)));
        }

        while (pos_ < text_.size()) {
            skip_whitespace();
            std::string key;
            if (pos_ < text_.size() && text_[pos_] == '"') {
                key = parse_string();
            } else {
                size_t kstart = pos_;
                while (pos_ < text_.size() && text_[pos_] != ':' && !std::isspace(static_cast<unsigned char>(text_[pos_]))) {
                    pos_++;
                }
                key = std::string(text_.substr(kstart, pos_ - kstart));
            }

            skip_whitespace();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                throw TypeError("Expected ':' after key in dictionary");
            }
            pos_++; // skip ':'

            skip_whitespace();
            Value val = parse_value();
            map[std::move(key)] = std::move(val);

            skip_whitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                pos_++;
            } else if (pos_ < text_.size() && text_[pos_] == '}') {
                pos_++;
                break;
            } else {
                throw TypeError("Expected ',' or '}' in dictionary");
            }
        }
        return Value(Dictionary(std::move(map)));
    }

    std::string_view text_;
    size_t pos_;
};

Value Value::parse_inferred(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return Value("");
    }
    ValueParser parser(text);
    return parser.parse_value();
}

Value Value::deserialize(Type expected_type, std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }

    switch (expected_type) {
        case Type::Bool: {
            std::string lower(text);
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") return Value(true);
            if (lower == "false" || lower == "0" || lower == "no" || lower == "off") return Value(false);
            throw TypeError("Invalid boolean string: " + std::string(text));
        }
        case Type::Int64: {
            int64_t n = 0;
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
            if (ec == std::errc()) return Value(n);
            throw TypeError("Invalid int64 string: " + std::string(text));
        }
        case Type::Double: {
            double d = 0.0;
            std::istringstream ss{std::string(text)};
            if (ss >> d) return Value(d);
            throw TypeError("Invalid double string: " + std::string(text));
        }
        case Type::String: {
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
                ValueParser parser(text);
                return Value(parser.parse_string());
            }
            return Value(std::string(text));
        }
        case Type::StringList: {
            if (text.starts_with('[') && text.ends_with(']')) {
                ValueParser parser(text);
                Value parsed = parser.parse_value();
                if (parsed.is_string_list()) return parsed;
            }
            // Semicolon or comma separated fallback
            std::vector<std::string> items;
            std::string item;
            std::istringstream ss{std::string(text)};
            char delimiter = (text.find(';') != std::string_view::npos) ? ';' : ',';
            while (std::getline(ss, item, delimiter)) {
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.front()))) item.erase(0, 1);
                while (!item.empty() && std::isspace(static_cast<unsigned char>(item.back()))) item.pop_back();
                if (!item.empty()) items.push_back(item);
            }
            return Value(items);
        }
        case Type::Enum: {
            if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
                ValueParser parser(text);
                return Value::make_enum(parser.parse_string());
            }
            return Value::make_enum(std::string(text));
        }
        case Type::Color: {
            std::string_view hex = text;
            if (hex.size() >= 2 && hex.front() == '"' && hex.back() == '"') {
                hex.remove_prefix(1);
                hex.remove_suffix(1);
            }
            auto c = Color::from_hex_string(hex);
            if (c) return Value(*c);
            throw TypeError("Invalid color string: " + std::string(text));
        }
        case Type::Rect: {
            auto r = Rect::from_string(text);
            if (r) return Value(*r);
            throw TypeError("Invalid rect string: " + std::string(text));
        }
        case Type::Dictionary: {
            ValueParser parser(text);
            Value val = parser.parse_value();
            if (val.is_dictionary()) return val;
            throw TypeError("Expected dictionary JSON format: " + std::string(text));
        }
    }
    throw TypeError("Unknown expected Type in deserialize");
}

} // namespace broconf
