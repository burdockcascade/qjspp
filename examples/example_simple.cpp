#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    // 1. Evaluate math and read a double
    auto res1 = engine.eval("const a = 10; const b = 32.5; a + b;");
    if (res1) {
        if (res1->is_number()) {
            std::println("Result as double: {}", res1->to_double());
        }
    } else {
        std::println("JS Error: {}", res1.error().to_string());
    }

    // 2. Evaluate string
    auto res2 = engine.eval("'Hello ' + 'from C++!'");
    if (res2) {
        std::println("Result as string: {}", res2->to_string());
    }

    // 3. Exception handling
    auto res3 = engine.eval("throw new Error('Something went wrong!');");
    if (!res3) {
        std::println("Caught Error:\n{}", res3.error().to_string());
    }

    return 0;
}