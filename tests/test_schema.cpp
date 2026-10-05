#include "broconf/schema.h"

#include <cassert>
#include <iostream>

using namespace broconf;

void test_key_schema_validation() {
    // Int range validation
    KeySchema int_ks;
    int_ks.name = "volume";
    int_ks.type = Type::Int64;
    int_ks.default_value = Value(static_cast<int64_t>(50));
    int_ks.min_value = Value(static_cast<int64_t>(0));
    int_ks.max_value = Value(static_cast<int64_t>(100));

    std::string err;
    assert(int_ks.validate(Value(static_cast<int64_t>(50)), &err));
    assert(int_ks.validate(Value(static_cast<int64_t>(0)), &err));
    assert(int_ks.validate(Value(static_cast<int64_t>(100)), &err));

    assert(!int_ks.validate(Value(static_cast<int64_t>(-1)), &err));
    assert(!err.empty());
    assert(!int_ks.validate(Value(static_cast<int64_t>(101)), &err));

    // Type mismatch
    assert(!int_ks.validate(Value("fifty"), &err));

    // Double range validation
    KeySchema dbl_ks;
    dbl_ks.name = "scale";
    dbl_ks.type = Type::Double;
    dbl_ks.default_value = Value(1.0);
    dbl_ks.min_value = Value(0.5);
    dbl_ks.max_value = Value(3.0);

    assert(dbl_ks.validate(Value(1.5), &err));
    assert(!dbl_ks.validate(Value(0.2), &err));
    assert(!dbl_ks.validate(Value(4.0), &err));

    // Enum allowed values validation
    KeySchema enum_ks;
    enum_ks.name = "placement";
    enum_ks.type = Type::Enum;
    enum_ks.default_value = Value::make_enum("center");
    enum_ks.allowed_enum_values = {"top", "bottom", "left", "right", "center"};

    assert(enum_ks.validate(Value::make_enum("top"), &err));
    assert(enum_ks.validate(Value("bottom"), &err));
    assert(!enum_ks.validate(Value::make_enum("diagonal"), &err));
    assert(!enum_ks.validate(Value("diagonal"), &err));

    // Custom validator
    KeySchema custom_ks;
    custom_ks.name = "even_number";
    custom_ks.type = Type::Int64;
    custom_ks.default_value = Value(static_cast<int64_t>(2));
    custom_ks.custom_validator = [](const Value& v) {
        return v.get_int() % 2 == 0;
    };

    assert(custom_ks.validate(Value(static_cast<int64_t>(4)), &err));
    assert(!custom_ks.validate(Value(static_cast<int64_t>(5)), &err));

    std::cout << "[PASS] test_key_schema_validation\n";
}

void test_schema_builder() {
    Schema schema("org.bro.desktop.interface");
    assert(schema.id() == "org.bro.desktop.interface");
    assert(schema.path() == "/org/bro/desktop/interface/");

    schema.add_bool("dark-mode", false, "Dark theme", "Whether dark mode is enabled")
          .add_int("font-size", 11, 6, 48, "Font size", "System default font size in points")
          .add_double("animation-speed", 1.0, 0.1, 5.0)
          .add_string("icon-theme", "BroIcons")
          .add_string_list("pinned-apps", {"org.bro.terminal", "org.bro.files"})
          .add_enum("clock-format", "24h", {"12h", "24h"})
          .add_color("accent-color", Color(53, 132, 228, 255))
          .add_rect("default-window-rect", Rect(100, 100, 800, 600))
          .add_dictionary("user-customizations");

    assert(schema.has_key("dark-mode"));
    assert(schema.has_key("font-size"));
    assert(schema.has_key("accent-color"));
    assert(!schema.has_key("nonexistent"));

    const auto* ks = schema.get_key("accent-color");
    assert(ks != nullptr);
    assert(ks->type == Type::Color);
    assert(ks->default_value.get_color() == Color(53, 132, 228, 255));

    std::string err;
    assert(schema.validate("font-size", Value(static_cast<int64_t>(12)), &err));
    assert(!schema.validate("font-size", Value(static_cast<int64_t>(3)), &err));
    assert(!schema.validate("nonexistent", Value(1), &err));

    std::cout << "[PASS] test_schema_builder\n";
}

void test_schema_registry() {
    SchemaRegistry registry;

    auto s1 = std::make_shared<Schema>("org.bro.desktop.interface");
    s1->add_string("theme", "BroDefault");

    auto s2 = std::make_shared<Schema>("org.bro.desktop.wm", "/org/bro/desktop/wm/");
    s2->add_bool("enable-tiling", true);

    registry.register_schema(s1);
    registry.register_schema(s2);

    assert(registry.find_schema("org.bro.desktop.interface") != nullptr);
    assert(registry.find_schema("/org/bro/desktop/interface/") != nullptr);
    assert(registry.find_schema("/org/bro/desktop/interface") != nullptr);

    assert(registry.find_schema("org.bro.desktop.wm") != nullptr);
    assert(registry.find_schema("/org/bro/desktop/wm/") != nullptr);

    assert(registry.find_schema("org.bro.nonexistent") == nullptr);

    auto list = registry.list_schemas();
    assert(list.size() == 2);

    registry.clear();
    assert(registry.list_schemas().empty());

    std::cout << "[PASS] test_schema_registry\n";
}

int main() {
    test_key_schema_validation();
    test_schema_builder();
    test_schema_registry();
    std::cout << "All schema tests passed successfully.\n";
    return 0;
}
