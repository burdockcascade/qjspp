#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    auto arr = engine.make_array();
    arr.set(0, engine.make_string("Apple"));
    arr.set(1, engine.make_string("Banana"));

    std::println("Index 0: {}", arr.get(0).to_string());
    std::println("Index 1: {}", arr.get(1).to_string());

    return 0;
}