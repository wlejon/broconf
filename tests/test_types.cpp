#include "broconf/types.h"
#include "broconf/value.h"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace broconf;

void test_color() {
    Color c1(255, 128, 64, 255);
    assert(c1.r == 255 && c1.g == 128 && c1.b == 64 && c1.a == 255);
    assert(c1.to_hex_string() == "#ff8040");

    Color c2(0, 0, 0, 128);
    assert(c2.to_hex_string(true) == "#00000080");

    auto parsed1 = Color::from_hex_string("#ff8040");
    assert(parsed1.has_value());
    assert(*parsed1 == c1);

    auto parsed2 = Color::from_hex_string("#00000080");
    assert(parsed2.has_value());
    assert(*parsed2 == c2);

    auto parsed3 = Color::from_hex_string("#f0a");
    assert(parsed3.has_value());
    assert(parsed3->r == 0xff && parsed3->g == 0x00 && parsed3->b == 0xaa && parsed3->a == 255);

    auto parsed_invalid = Color::from_hex_string("not-a-color");
    assert(!parsed_invalid.has_value());

    std::cout << "[PASS] test_color\n";
}

void test_rect() {
    Rect r1(10, 20, 800, 600);
    assert(r1.x == 10 && r1.y == 20 && r1.width == 800 && r1.height == 600);
    assert(r1.to_string() == "10, 20, 800, 600");

    auto p1 = Rect::from_string("10, 20, 800, 600");
    assert(p1.has_value());
    assert(*p1 == r1);

    auto p2 = Rect::from_string("[10, 20, 800, 600]");
    assert(p2.has_value());
    assert(*p2 == r1);

    auto p_invalid = Rect::from_string("10, 20, 800");
    assert(!p_invalid.has_value());

    std::cout << "[PASS] test_rect\n";
}

void test_enum_value() {
    EnumValue e1("dark", 1);
    EnumValue e2("dark", 1);
    EnumValue e3("light", 0);

    assert(e1 == e2);
    assert(!(e1 == e3));
    assert(e1.name == "dark");
    assert(e1.value == 1);

    std::cout << "[PASS] test_enum_value\n";
}

void test_dictionary() {
    Dictionary dict;
    assert(dict.empty());
    assert(dict.size() == 0);

    dict.set("key1", Value(true));
    dict.set("key2", Value(static_cast<int64_t>(42)));
    dict.set("key3", Value("hello"));

    assert(!dict.empty());
    assert(dict.size() == 3);
    assert(dict.contains("key1"));
    assert(dict.contains("key2"));
    assert(dict.contains("key3"));
    assert(!dict.contains("missing"));

    assert(dict.get("key1")->get_bool() == true);
    assert(dict.get("key2")->get_int() == 42);
    assert(dict.get("key3")->get_string() == "hello");

    Dictionary copy = dict;
    assert(copy == dict);

    assert(dict.remove("key1"));
    assert(!dict.contains("key1"));
    assert(dict.size() == 2);
    assert(copy != dict);

    dict.clear();
    assert(dict.empty());

    std::cout << "[PASS] test_dictionary\n";
}

void test_value_types() {
    // 1. Bool
    Value vb(true);
    assert(vb.type() == Type::Bool);
    assert(vb.is_bool());
    assert(vb.get_bool() == true);
    assert(vb.serialize() == "true");
    assert(Value::deserialize(Type::Bool, "false").get_bool() == false);

    // 2. Int64
    Value vi(static_cast<int64_t>(123456789012LL));
    assert(vi.type() == Type::Int64);
    assert(vi.is_int());
    assert(vi.get_int() == 123456789012LL);
    assert(vi.serialize() == "123456789012");
    assert(Value::deserialize(Type::Int64, "-42").get_int() == -42);

    // 3. Double
    Value vd(3.14159);
    assert(vd.type() == Type::Double);
    assert(vd.is_double());
    assert(std::abs(vd.get_double() - 3.14159) < 0.0001);
    Value vd_deser = Value::deserialize(Type::Double, "2.71828");
    assert(std::abs(vd_deser.get_double() - 2.71828) < 0.0001);

    // 4. String
    Value vs(std::string("Hello \"world\"!\nNew line"));
    assert(vs.type() == Type::String);
    assert(vs.is_string());
    assert(vs.get_string() == "Hello \"world\"!\nNew line");
    std::string s_serialized = vs.serialize();
    Value vs_deser = Value::deserialize(Type::String, s_serialized);
    assert(vs_deser.get_string() == vs.get_string());

    // 5. StringList
    std::vector<std::string> list = {"apple", "banana", "cherry"};
    Value vsl(list);
    assert(vsl.type() == Type::StringList);
    assert(vsl.is_string_list());
    assert(vsl.get_string_list().size() == 3);
    assert(vsl.get_string_list()[1] == "banana");
    std::string sl_serialized = vsl.serialize();
    Value vsl_deser = Value::deserialize(Type::StringList, sl_serialized);
    assert(vsl_deser.get_string_list() == list);

    // 6. Enum
    Value ve = Value::make_enum("center", 2);
    assert(ve.type() == Type::Enum);
    assert(ve.is_enum());
    assert(ve.get_enum().name == "center");
    Value ve_deser = Value::deserialize(Type::Enum, "\"right\"");
    assert(ve_deser.get_enum().name == "right");

    // 7. Color
    Value vc = Value::make_color(10, 20, 30, 255);
    assert(vc.type() == Type::Color);
    assert(vc.is_color());
    assert(vc.get_color() == Color(10, 20, 30, 255));
    std::string col_ser = vc.serialize();
    Value vc_deser = Value::deserialize(Type::Color, col_ser);
    assert(vc_deser.get_color() == vc.get_color());

    // 8. Rect
    Value vr = Value::make_rect(100, 150, 640, 480);
    assert(vr.type() == Type::Rect);
    assert(vr.is_rect());
    assert(vr.get_rect() == Rect(100, 150, 640, 480));
    std::string rect_ser = vr.serialize();
    Value vr_deser = Value::deserialize(Type::Rect, rect_ser);
    assert(vr_deser.get_rect() == vr.get_rect());

    // 9. Dictionary
    Dictionary d;
    d.set("enabled", Value(true));
    d.set("count", Value(static_cast<int64_t>(10)));
    d.set("title", Value("Bro"));
    Value vdict(d);
    assert(vdict.type() == Type::Dictionary);
    assert(vdict.is_dictionary());
    std::string dict_ser = vdict.serialize();
    Value vdict_deser = Value::deserialize(Type::Dictionary, dict_ser);
    assert(vdict_deser.get_dictionary().contains("enabled"));
    assert(vdict_deser.get_dictionary().get("enabled")->get_bool() == true);
    assert(vdict_deser.get_dictionary().get("count")->get_int() == 10);
    assert(vdict_deser.get_dictionary().get("title")->get_string() == "Bro");

    // Inferred parsing
    assert(Value::parse_inferred("true").get_bool() == true);
    assert(Value::parse_inferred("100").get_int() == 100);
    assert(std::abs(Value::parse_inferred("3.5").get_double() - 3.5) < 0.001);
    assert(Value::parse_inferred("\"test\"").get_string() == "test");
    assert(Value::parse_inferred("#aabbcc").get_color() == Color(0xaa, 0xbb, 0xcc, 255));

    // Type error checks
    bool threw = false;
    try {
        (void)vb.get_int();
    } catch (const TypeError&) {
        threw = true;
    }
    assert(threw);

    std::cout << "[PASS] test_value_types\n";
}

int main() {
    test_color();
    test_rect();
    test_enum_value();
    test_dictionary();
    test_value_types();
    std::cout << "All types tests passed successfully.\n";
    return 0;
}
