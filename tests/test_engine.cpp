#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "../include/qjspp.hpp"

TEST_CASE("Engine Lifecycle and Resource Management", "[engine]") {
    SECTION("Default construction") {
        auto engine = qjspp::Engine::micro();
        CHECK(engine.runtime() != nullptr);
        CHECK(engine.context() != nullptr);
    }

    SECTION("Construction with custom memory limit and stack size") {
        auto engine = qjspp::Engine(1024 * 1024, 65536); // 1MB limit
        CHECK(engine.runtime() != nullptr);
        CHECK(engine.context() != nullptr);
    }

    SECTION("Move construction") {
        auto engine1 = qjspp::Engine::micro();;
        JSRuntime* original_rt = engine1.runtime();
        JSContext* original_ctx = engine1.context();

        qjspp::Engine engine2(std::move(engine1));

        CHECK(engine2.runtime() == original_rt);
        CHECK(engine2.context() == original_ctx);
        CHECK(engine1.runtime() == nullptr);
        CHECK(engine1.context() == nullptr);
    }

    SECTION("Move assignment") {
        auto engine1 = qjspp::Engine::micro();;
        auto engine2 = qjspp::Engine::micro();;

        JSRuntime* original_rt1 = engine1.runtime();
        JSContext* original_ctx1 = engine1.context();

        engine2 = std::move(engine1);

        CHECK(engine2.runtime() == original_rt1);
        CHECK(engine2.context() == original_ctx1);
        CHECK(engine1.runtime() == nullptr);
        CHECK(engine1.context() == nullptr);
    }

    SECTION("Manual Garbage Collection invocation") {
        auto engine = qjspp::Engine::micro();
        // Should execute without crashing
        REQUIRE_NOTHROW(engine.gc());
    }
}

TEST_CASE("Engine Core Execution - eval()", "[engine]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Evaluating valid expressions returning standard values") {
        auto res_int = engine.eval("10 + 20");
        REQUIRE(res_int.has_value());
        CHECK(res_int->to_int() == 30);

        auto res_str = engine.eval("'Hello ' + 'World'");
        REQUIRE(res_str.has_value());
        CHECK(res_str->to_string() == "Hello World");

        auto res_bool = engine.eval("5 > 3");
        REQUIRE(res_bool.has_value());
        CHECK(res_bool->to_bool() == true);
    }

    SECTION("Evaluating code that throws JS exceptions") {
        auto res = engine.eval("throw new Error('Custom JS Error');");
        REQUIRE_FALSE(res.has_value());

        const qjspp::JsError& err = res.error();
        CHECK(err.message.find("Custom JS Error") != std::string::npos);
    }

    SECTION("Evaluating invalid syntax") {
        auto res = engine.eval("const x = ;");
        REQUIRE_FALSE(res.has_value());
        CHECK_FALSE(res.error().message.empty());
    }

    SECTION("State persistence between eval calls") {
        auto res1 = engine.eval("var globalVar = 42;");
        REQUIRE(res1.has_value());

        auto res2 = engine.eval("globalVar + 8;");
        REQUIRE(res2.has_value());
        CHECK(res2->to_int() == 50);
    }
}

TEST_CASE("Engine Core Execution - exec()", "[engine]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Executing valid scripts") {
        qjspp::Value val = engine.exec("let a = 10; let b = 20; a * b;");
        CHECK(val.is_number());
        CHECK(val.to_int() == 200);
    }

    SECTION("Executing code that throws an exception throws std::runtime_error") {
        CHECK_THROWS_AS(
            engine.exec("throw new Error('Fatal script execution failure');"),
            std::runtime_error
        );

        CHECK_THROWS_WITH(
            engine.exec("throw new Error('Fatal script execution failure');"),
            Catch::Matchers::ContainsSubstring("Fatal script execution failure")
        );
    }
}

TEST_CASE("Engine Value Factory Helpers", "[engine]") {
    auto engine = qjspp::Engine::micro();

    SECTION("Primitives and Containers") {
        CHECK(engine.make_undefined().is_undefined());
        CHECK(engine.make_null().is_null());

        qjspp::Value b = engine.make_bool(true);
        CHECK(b.is_bool());
        CHECK(b.to_bool() == true);

        qjspp::Value i = engine.make_int(42);
        CHECK(i.is_number());
        CHECK(i.to_int() == 42);

        constexpr int64_t JS_MAX_SAFE_INTEGER = (1LL << 53) - 1;
        qjspp::Value l = engine.make_long(JS_MAX_SAFE_INTEGER);
        CHECK(l.to_long() == JS_MAX_SAFE_INTEGER);

        qjspp::Value d = engine.make_double(3.14159);
        CHECK(d.is_number());
        CHECK(d.to_double() == Catch::Approx(3.14159));

        qjspp::Value s = engine.make_string("FactoryString");
        CHECK(s.is_string());
        CHECK(s.to_string() == "FactoryString");

        qjspp::Value obj = engine.make_object();
        CHECK(obj.is_object());

        qjspp::Value arr = engine.make_array();
        CHECK(arr.is_array());
    }

    SECTION("Overloaded make_value methods") {
        qjspp::Value val_int = engine.make_value(100);
        CHECK(val_int.to_int() == 100);

        qjspp::Value val_double = engine.make_value(2.718);
        CHECK(val_double.to_double() == Catch::Approx(2.718));

        qjspp::Value val_str = engine.make_value(std::string_view("Overload"));
        CHECK(val_str.to_string() == "Overload");
    }

    SECTION("Native function creation via Engine") {
        qjspp::Value fn = engine.make_function([&engine](const std::vector<qjspp::Value>& args) {
            if (args.empty()) return engine.make_int(0);
            return engine.make_int(args[0].to_int() + 100);
        });

        qjspp::Value res = fn.call({engine.make_int(50)});
        CHECK(res.to_int() == 150);
    }
}