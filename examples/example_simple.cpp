#include <print>
#include "../include/engine.hpp"

int main() {
    qjspp::Engine engine;

    // 1. Evaluate simple math
    auto res1 = engine.eval("const a = 10; let b = 20; b = 30; a + b;");
    if (res1) {
        std::println("Result: {}", res1.value());
    } else {
        std::println("JS Error: {}", res1.error().message);
        if (!res1.error().stack.empty()) {
            std::println("Stack:\n {}", res1.error().stack);
        }
    }

    // 2. Catch syntax errors gracefully
    auto res2 = engine.eval("const broken = {");
    if (res2) {
        std::println("Result: {}", res2.value());
    } else {
        std::println("JS Error: {}", res2.error().message);
        if (!res2.error().stack.empty()) {
            std::println("Stack:\n {}", res2.error().stack);
        }
    }

    return 0;
}