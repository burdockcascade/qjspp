#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    // 1. Calling a global arrow function
    auto fn_res = engine.eval("(a, b) => 'Result: ' + (a * b)");
    if (fn_res) {
        qjspp::Value multiply_fn = std::move(fn_res.value());

        // Invoke call() using explicit Engine primitive builders
        qjspp::Value result = multiply_fn.call({
            engine.make_double(6),
            engine.make_double(7)
        });

        std::println("{}", result.to_string()); // Outputs: "Result: 42"
    }

    // 2. Calling a method with a custom 'this' context
    auto greeter_res = engine.eval("function greet(greeting) { return greeting + ', ' + this.name; } greet;");
    auto user_obj = engine.eval("({ name: 'Alice' })");

    if (greeter_res && user_obj) {
        qjspp::Value greet_fn = std::move(greeter_res.value());
        qjspp::Value user = std::move(user_obj.value());

        qjspp::Value result = greet_fn.call_method(user, {
            engine.make_string("Hello")
        });

        std::println("{}", result.to_string()); // Outputs: "Hello, Alice"
    }

    return 0;
}