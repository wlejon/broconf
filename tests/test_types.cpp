#include "broconf/types.h"
#include "broconf/value.h"

#include "check.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace broconf;

void test_color() {
    Color c1(255, 128, 64, 255);
    REQUIRE(c1.r == 255 && c1.g == 128 && c1.b == 64 && c1.a == 255);
    REQUIRE(c1.to_hex_string() == "#ff8040");

    Color c2(0, 0, 0, 128);
    REQUIRE(c2.to_hex_string(true) == "#00000080");

    auto parsed1 = Color::from_hex_string("#ff8040");
    REQUIRE(parsed1.has_value());
    REQUIRE(*parsed1 == c1);

    auto parsed2 = Color::from_hex_string("#00000080");
    REQUIRE(parsed2.has_value());
    REQUIRE(*parsed2 == c2);

    auto parsed3 = Color::from_hex_string("#f0a");
    REQUIRE(parsed3.has_value());
    REQUIRE(parsed3->r == 0xff && parsed3->g == 0x00 && parsed3->b == 0xaa && parsed3->a == 255);

    auto parsed_invalid = Color::from_hex_string("not-a-color");
    REQUIRE(!parsed_invalid.has_value());

    std::cout << "[PASS] test_color\n";
}

void test_rect() {
    Rect r1(10, 20, 800, 600);
    REQUIRE(r1.x == 10 && r1.y == 20 && r1.width == 800 && r1.height == 600);
    REQUIRE(r1.to_string() == "10, 20, 800, 600");

    auto p1 = Rect::from_string("10, 20, 800, 600");
    REQUIRE(p1.has_value());
    REQUIRE(*p1 == r1);

    auto p2 = Rect::from_string("[10, 20, 800, 600]");
    REQUIRE(p2.has_value());
    REQUIRE(*p2 == r1);

    auto p_invalid = Rect::from_string("10, 20, 800");
    REQUIRE(!p_invalid.has_value());

    std::cout << "[PASS] test_rect\n";
}

void test_enum_value() {
    EnumValue e1("dark", 1);
    EnumValue e2("dark", 1);
    EnumValue e3("light", 0);

    REQUIRE(e1 == e2);
    REQUIRE(!(e1 == e3));
    REQUIRE(e1.name == "dark");
    REQUIRE(e1.value == 1);

    std::cout << "[PASS] test_enum_value\n";
}

void test_dictionary() {
    Dictionary dict;
    REQUIRE(dict.empty());
    REQUIRE(dict.size() == 0);

    dict.set("key1", Value(true));
    dict.set("key2", Value(static_cast<int64_t>(42)));
    dict.set("key3", Value("hello"));

    REQUIRE(!dict.empty());
    REQUIRE(dict.size() == 3);
    REQUIRE(dict.contains("key1"));
    REQUIRE(dict.contains("key2"));
    REQUIRE(dict.contains("key3"));
    REQUIRE(!dict.contains("missing"));

    REQUIRE(dict.get("key1")->get_bool() == true);
    REQUIRE(dict.get("key2")->get_int() == 42);
    REQUIRE(dict.get("key3")->get_string() == "hello");

    Dictionary copy = dict;
    REQUIRE(copy == dict);

    REQUIRE(dict.remove("key1"));
    REQUIRE(!dict.contains("key1"));
    REQUIRE(dict.size() == 2);
    REQUIRE(copy != dict);

    dict.clear();
    REQUIRE(dict.empty());

    std::cout << "[PASS] test_dictionary\n";
}

void test_value_types() {
    // 1. Bool
    Value vb(true);
    REQUIRE(vb.type() == Type::Bool);
    REQUIRE(vb.is_bool());
    REQUIRE(vb.get_bool() == true);
    REQUIRE(vb.serialize() == "true");
    REQUIRE(Value::deserialize(Type::Bool, "false").get_bool() == false);

    // 2. Int64
    Value vi(static_cast<int64_t>(123456789012LL));
    REQUIRE(vi.type() == Type::Int64);
    REQUIRE(vi.is_int());
    REQUIRE(vi.get_int() == 123456789012LL);
    REQUIRE(vi.serialize() == "123456789012");
    REQUIRE(Value::deserialize(Type::Int64, "-42").get_int() == -42);

    // 3. Double
    Value vd(3.14159);
    REQUIRE(vd.type() == Type::Double);
    REQUIRE(vd.is_double());
    REQUIRE(std::abs(vd.get_double() - 3.14159) < 0.0001);
    Value vd_deser = Value::deserialize(Type::Double, "2.71828");
    REQUIRE(std::abs(vd_deser.get_double() - 2.71828) < 0.0001);

    // 4. String
    Value vs(std::string("Hello \"world\"!\nNew line"));
    REQUIRE(vs.type() == Type::String);
    REQUIRE(vs.is_string());
    REQUIRE(vs.get_string() == "Hello \"world\"!\nNew line");
    std::string s_serialized = vs.serialize();
    Value vs_deser = Value::deserialize(Type::String, s_serialized);
    REQUIRE(vs_deser.get_string() == vs.get_string());

    // 5. StringList
    std::vector<std::string> list = {"apple", "banana", "cherry"};
    Value vsl(list);
    REQUIRE(vsl.type() == Type::StringList);
    REQUIRE(vsl.is_string_list());
    REQUIRE(vsl.get_string_list().size() == 3);
    REQUIRE(vsl.get_string_list()[1] == "banana");
    std::string sl_serialized = vsl.serialize();
    Value vsl_deser = Value::deserialize(Type::StringList, sl_serialized);
    REQUIRE(vsl_deser.get_string_list() == list);

    // 6. Enum
    Value ve = Value::make_enum("center", 2);
    REQUIRE(ve.type() == Type::Enum);
    REQUIRE(ve.is_enum());
    REQUIRE(ve.get_enum().name == "center");
    Value ve_deser = Value::deserialize(Type::Enum, "\"right\"");
    REQUIRE(ve_deser.get_enum().name == "right");

    // 7. Color
    Value vc = Value::make_color(10, 20, 30, 255);
    REQUIRE(vc.type() == Type::Color);
    REQUIRE(vc.is_color());
    REQUIRE(vc.get_color() == Color(10, 20, 30, 255));
    std::string col_ser = vc.serialize();
    Value vc_deser = Value::deserialize(Type::Color, col_ser);
    REQUIRE(vc_deser.get_color() == vc.get_color());

    // 8. Rect
    Value vr = Value::make_rect(100, 150, 640, 480);
    REQUIRE(vr.type() == Type::Rect);
    REQUIRE(vr.is_rect());
    REQUIRE(vr.get_rect() == Rect(100, 150, 640, 480));
    std::string rect_ser = vr.serialize();
    Value vr_deser = Value::deserialize(Type::Rect, rect_ser);
    REQUIRE(vr_deser.get_rect() == vr.get_rect());

    // 9. Dictionary
    Dictionary d;
    d.set("enabled", Value(true));
    d.set("count", Value(static_cast<int64_t>(10)));
    d.set("title", Value("Bro"));
    Value vdict(d);
    REQUIRE(vdict.type() == Type::Dictionary);
    REQUIRE(vdict.is_dictionary());
    std::string dict_ser = vdict.serialize();
    Value vdict_deser = Value::deserialize(Type::Dictionary, dict_ser);
    REQUIRE(vdict_deser.get_dictionary().contains("enabled"));
    REQUIRE(vdict_deser.get_dictionary().get("enabled")->get_bool() == true);
    REQUIRE(vdict_deser.get_dictionary().get("count")->get_int() == 10);
    REQUIRE(vdict_deser.get_dictionary().get("title")->get_string() == "Bro");

    // Inferred parsing
    REQUIRE(Value::parse_inferred("true").get_bool() == true);
    REQUIRE(Value::parse_inferred("100").get_int() == 100);
    REQUIRE(std::abs(Value::parse_inferred("3.5").get_double() - 3.5) < 0.001);
    REQUIRE(Value::parse_inferred("\"test\"").get_string() == "test");
    REQUIRE(Value::parse_inferred("#aabbcc").get_color() == Color(0xaa, 0xbb, 0xcc, 255));

    // Type error checks
    bool threw = false;
    try {
        (void)vb.get_int();
    } catch (const TypeError&) {
        threw = true;
    }
    REQUIRE(threw);

    std::cout << "[PASS] test_value_types\n";
}

void test_double_round_trip() {
    // Every double survives serialize -> deserialize exactly.
    for (double d : {0.1, 1.0 / 3.0, 2.0, -1e-300, 1.7976931348623157e308, 123456789.123456789}) {
        Value back = Value::deserialize(Type::Double, Value(d).serialize());
        CHECK_EQ(back.get_double(), d);
    }
    CHECK_EQ(Value(0.1).serialize(), std::string("0.1"));
    CHECK_EQ(Value(2.0).serialize(), std::string("2.0"));
    CHECK(std::isnan(Value::deserialize(Type::Double, Value(std::nan("")).serialize()).get_double()));
    CHECK_EQ(Value::deserialize(Type::Double, "inf").get_double(), std::numeric_limits<double>::infinity());
    CHECK_EQ(Value::deserialize(Type::Double, "-inf").get_double(), -std::numeric_limits<double>::infinity());

    // Trailing garbage is not an integer.
    bool threw = false;
    try {
        (void)Value::deserialize(Type::Int64, "12abc");
    } catch (const TypeError&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_color();
    test_rect();
    test_enum_value();
    test_dictionary();
    test_value_types();
    test_double_round_trip();
    return bstest::finish("test_types");
}
