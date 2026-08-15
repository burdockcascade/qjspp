#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    // 1. Create a native JavaScript function directly from a C++ lambda
    auto native_mult = engine.make_function([&engine](const std::vector<qjspp::Value>& args) {
        if (args.size() < 2) return engine.make_string("Invalid args");

        double a = args[0].to_double();
        double b = args[1].to_double();

        std::string result = "Result: " + std::to_string(a * b);
        return engine.make_string(result);
    });

    // 2. Execute the function directly from C++ using .call()
    qjspp::Value res = native_mult.call({
        engine.make_double(6),
        engine.make_double(7)
    });

    std::println("{}", res.to_string()); // Outputs: "Result: 42.000000"

    // 3. Bind the C++ function into the JavaScript environment
    qjspp::Value global_obj(engine.context(), JS_GetGlobalObject(engine.context()), /*dup=*/false);
    global_obj.set("multiplyAndFormat", native_mult);

    // Call it straight from JS!
    auto eval_res = engine.eval("multiplyAndFormat(10, 5)");
    if (eval_res) {
        std::println("JS Eval: {}", eval_res->to_string()); // Outputs: "Result: 50.000000"
    }

    return 0;
}