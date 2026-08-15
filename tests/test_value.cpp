#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../include/qjspp.hpp"

TEST_CASE("Value Primitive Factories and Conversions", "[value]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Integer creation and conversion") {
        qjspp::Value v = engine.make_int(123);
        CHECK(v.is_number());
        CHECK_FALSE(v.is_string());
        CHECK_FALSE(v.is_bool());
        CHECK(v.to_int() == 123);
        CHECK(v.to_double() == 123.0);
    }

    SECTION("Double creation and conversion") {
        qjspp::Value v = engine.make_double(123.456);
        CHECK(v.is_number());
        CHECK(v.to_double() == Catch::Approx(123.456));
    }

    SECTION("String creation and conversion") {
        qjspp::Value v = engine.make_string("QuickJS");
        CHECK(v.is_string());
        CHECK(v.to_string() == "QuickJS");
    }

    SECTION("Boolean creation and conversion") {
        qjspp::Value v_true = engine.make_bool(true);
        qjspp::Value v_false = engine.make_bool(false);

        CHECK(v_true.is_bool());
        CHECK(v_true.to_bool() == true);

        CHECK(v_false.is_bool());
        CHECK(v_false.to_bool() == false);
    }

    SECTION("Null creation and type checking") {
        qjspp::Value v = engine.make_null();
        CHECK(v.is_null());
        CHECK_FALSE(v.is_undefined());
        CHECK_FALSE(v.is_object());
    }

    SECTION("Undefined creation and type checking") {
        qjspp::Value v = engine.make_undefined();
        CHECK(v.is_undefined());
        CHECK_FALSE(v.is_null());
    }

    SECTION("Exception value checking") {
        auto res = engine.eval("throw new Error('Test Exception');");
        CHECK_FALSE(res.has_value());
        CHECK(res.error().message.find("Test Exception") != std::string::npos);
    }
}

TEST_CASE("Value Lifetime, Ownership, and Move Semantics", "[value]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Clone duplicates ref count safely") {
        qjspp::Value original = engine.make_string("CloneMe");
        qjspp::Value cloned = original.clone();

        CHECK(original.is_string());
        CHECK(cloned.is_string());
        CHECK(original.to_string() == "CloneMe");
        CHECK(cloned.to_string() == "CloneMe");
    }

    SECTION("Move construction transfers ownership") {
        qjspp::Value original = engine.make_string("MoveMe");
        qjspp::Value moved(std::move(original));

        CHECK(moved.is_string());
        CHECK(moved.to_string() == "MoveMe");
        CHECK(original.is_undefined()); // Original is reset
    }

    SECTION("Move assignment transfers ownership") {
        qjspp::Value original = engine.make_int(999);
        qjspp::Value target = engine.make_string("Temp");

        target = std::move(original);

        CHECK(target.is_number());
        CHECK(target.to_int() == 999);
        CHECK(original.is_undefined());
    }

    SECTION("Release detaches JSValue without freeing") {
        qjspp::Value v = engine.make_int(42);
        JSContext* ctx = v.context();
        JSValue raw = v.release();

        CHECK(v.is_undefined());
        CHECK(v.context() == nullptr);

        // Manually clean up raw JSValue since release gave up RAII control
        JS_FreeValue(ctx, raw);
    }
}

TEST_CASE("Value Object Property Operations", "[value]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Creating object and using set/get/has") {
        qjspp::Value obj = engine.make_object();
        CHECK(obj.is_object());

        obj.set("name", engine.make_string("Alice"));
        obj.set("age", engine.make_int(30));

        CHECK(obj.has("name"));
        CHECK(obj.has("age"));
        CHECK_FALSE(obj.has("non_existent"));

        CHECK(obj.get("name").to_string() == "Alice");
        CHECK(obj.get("age").to_int() == 30);
    }

    SECTION("Evaluating JS object and inspecting properties") {
        auto res = engine.eval("({ key: 'value', count: 10 })");
        REQUIRE(res.has_value());

        qjspp::Value obj = std::move(res.value());
        CHECK(obj.is_object());
        CHECK(obj.get("key").to_string() == "value");
        CHECK(obj.get("count").to_int() == 10);
    }
}

TEST_CASE("Value Array Operations", "[value]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Creating array and accessing via index") {
        qjspp::Value arr = engine.make_array();
        CHECK(arr.is_object());
        CHECK(arr.is_array());

        arr.set(0, engine.make_string("First"));
        arr.set(1, engine.make_string("Second"));

        CHECK(arr.get(0).to_string() == "First");
        CHECK(arr.get(1).to_string() == "Second");
        CHECK(arr.get("length").to_int() == 2);
    }
}

TEST_CASE("Value Function Calls", "[value]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Calling unbound JS function") {
        auto res = engine.eval("(a, b) => a + b");
        REQUIRE(res.has_value());

        qjspp::Value fn = std::move(res.value());
        qjspp::Value call_res = fn.call({
            engine.make_int(15),
            engine.make_int(27)
        });

        CHECK(call_res.is_number());
        CHECK(call_res.to_int() == 42);
    }

    SECTION("Calling JS method bound to custom 'this'") {
        auto fn_res = engine.eval("function greet(prefix) { return prefix + ' ' + this.name; } greet;");
        auto obj_res = engine.eval("({ name: 'Bob' })");

        REQUIRE(fn_res.has_value());
        REQUIRE(obj_res.has_value());

        qjspp::Value greet_fn = std::move(fn_res.value());
        qjspp::Value user_obj = std::move(obj_res.value());

        qjspp::Value call_res = greet_fn.call_method(user_obj, {
            engine.make_string("Hello")
        });

        CHECK(call_res.is_string());
        CHECK(call_res.to_string() == "Hello Bob");
    }
}

TEST_CASE("Value Native Function Creation", "[value]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Exposing C++ lambda as JS function and invoking it") {
        auto native_fn = engine.make_function([&engine](const std::vector<qjspp::Value>& args) {
            REQUIRE(args.size() == 2);
            int a = args[0].to_int();
            int b = args[1].to_int();
            return engine.make_int(a * b);
        });

        // Call directly from C++
        qjspp::Value res = native_fn.call({
            engine.make_int(6),
            engine.make_int(7)
        });
        CHECK(res.to_int() == 42);

        // Bind to JS global object and call from JS code
        qjspp::Value global_obj(engine.context(), JS_GetGlobalObject(engine.context()), false);
        global_obj.set("nativeMult", native_fn);

        auto eval_res = engine.eval("nativeMult(8, 9)");
        REQUIRE(eval_res.has_value());
        CHECK(eval_res->to_int() == 72);
    }
}