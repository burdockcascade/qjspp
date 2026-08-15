#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <qjspp.hpp>


struct Calculator {
    double value{0.0};
    Calculator(double initial) : value(initial) {}
    double add(double x) { value += x; return value; }
};

TEST_CASE("ModuleBuilder export functionality", "[module]") {
    qjspp::Engine engine;

    SECTION("Export primitive values, functions, and native classes to ES Module") {

        auto calc = engine.make_class<Calculator>("Calculator")
            .constructor([](const std::vector<qjspp::Value>& args) {
                double initial = args.empty() ? 0.0 : args[0].to_double();
                return std::make_unique<Calculator>(initial);
            })
            .instance_method("add", [](Calculator* calc, const std::vector<qjspp::Value>& args) {
                REQUIRE(!args.empty());
                return qjspp::Value::make_double(args[0].context(), calc->add(args[0].to_double()));
            })
            .property("value", [](JSContext* ctx, Calculator* calc) {
                return qjspp::Value::make_double(ctx, calc->value);
            })
            .build();

        // Build and register native module "math_utils"
        auto mod = engine.new_module("math_utils")
            .export_value("PI", engine.make_double(3.14159))
            .export_value("version", engine.make_string("1.0.0"))
            .export_function("add", [](const std::vector<qjspp::Value>& args) {
                REQUIRE(args.size() >= 2);
                double a = args[0].to_double();
                double b = args[1].to_double();
                return qjspp::Value::make_double(args[0].context(), a + b);
            })
            .export_class("Calculator", std::move(calc))
            .build();

        REQUIRE(mod != nullptr);

        // Evaluate JavaScript module importing our native bindings including exported class
        const char* js_code = R"(
            import { PI, version, add, Calculator } from "math_utils";

            const calc = new Calculator(10);
            const added = calc.add(5);

            globalThis.testResults = {
                pi: PI,
                ver: version,
                sum: add(10.5, 4.5),
                calcVal: calc.value,
                calcSum: added
            };
        )";

        auto result = engine.eval(js_code, "test.js", JS_EVAL_TYPE_MODULE);
        REQUIRE(result.has_value());

        // Verify the values produced inside JavaScript runtime
        auto global_obj = qjspp::Value(engine.context(), JS_GetGlobalObject(engine.context()), false);
        auto results = global_obj.get("testResults");

        REQUIRE(results.is_object());
        REQUIRE_THAT(results.get("pi").to_double(), Catch::Matchers::WithinRel(3.14159, 0.0001));
        REQUIRE(results.get("ver").to_string() == "1.0.0");
        REQUIRE(results.get("sum").to_double() == 15.0);
        REQUIRE(results.get("calcVal").to_double() == 15.0);
        REQUIRE(results.get("calcSum").to_double() == 15.0);
    }

    SECTION("Module throws on missing export access") {
        qjspp::ModuleBuilder builder(engine.context(), "empty_mod");
        builder.build();

        // Attempting to import an unexported identifier should fail module evaluation
        const char* js_code = R"(
            import { nonExistent } from "empty_mod";
        )";

        auto result = engine.eval(js_code, "test_fail.js", JS_EVAL_TYPE_MODULE);
        REQUIRE_FALSE(result.has_value());
    }
}