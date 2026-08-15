#include <print>
#include "engine.hpp"
#include "value.hpp"

int main() {
    qjspp::Engine engine;

    // 1. Evaluate an expression returning a string
    auto res1 = engine.eval("'QuickJS' + ' Wrapper'");
    if (res1) {
        // res1.value() is a qjspp::Value instance
        if (res1->is_string()) {
            std::println("Eval result: {}", res1->to_string());
        }
    } else {
        std::println("JS Error: {}", res1.error().to_string());
    }

    // 2. Evaluate primitive math numbers
    auto res2 = engine.eval("const a = 20; const b = 22.5; a + b;");
    if (res2 && res2->is_number()) {
        std::println("Number result: {}", res2->to_double());
    }

    // 3. Create native Value helpers bound to the engine context
    auto native_num = engine.make_double(100.5);
    auto native_str = engine.make_string("Hello from C++!");

    std::println("Native Double: {}", native_num.to_double());
    std::println("Native String: {}", native_str.to_string());

    // 4. Catch errors via std::expected
    auto res3 = engine.eval("const broken = {");
    if (!res3) {
        std::println("Caught Syntax Error: {}", res3.error().message);
    }

    // All Value instances clean up their QuickJS reference counts automatically!
    return 0;
}