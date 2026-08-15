#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    auto obj_res = engine.eval("({ name: 'Bob', age: 25, items: ['laptop', 'phone'] })");
    if (obj_res) {
        qjspp::Value person = std::move(obj_res.value());

        if (person.has("name")) {
            std::println("Name: {}", person.get("name").to_string());
            std::println("Age: {}", person.get("age").to_int());
        }

        // Reading array indices
        qjspp::Value items = person.get("items");
        std::println("First item: {}", items.get(0).to_string());
    }

    return 0;
}