#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    // 1. Reading object properties evaluated from JS
    auto obj_res = engine.eval("({ name: 'Bob', age: 25, items: ['laptop', 'phone'] })");
    if (obj_res) {
        qjspp::Value person = std::move(obj_res.value());

        if (person.has("name")) {
            std::println("Name: {}", person.get("name").to_string());
            std::println("Age: {}", person.get("age").to_int32());
        }

        // Reading array indices
        qjspp::Value items = person.get("items");
        std::println("First item: {}", items.get(0).to_string());
    }

    // 2. Modifying and constructing objects from C++
    auto new_obj = engine.make_object();
    new_obj.set("title", engine.make_string("Developer"));
    new_obj.set("salary", engine.make_double(95000.0));

    std::println("Title: {}", new_obj.get("title").to_string());
    std::println("Salary: {}", new_obj.get("salary").to_double());

    // 3. Modifying an array
    auto arr = engine.make_array();
    arr.set(0, engine.make_string("Apple"));
    arr.set(1, engine.make_string("Banana"));

    std::println("Index 0: {}", arr.get(0).to_string());
    std::println("Index 1: {}", arr.get(1).to_string());

    return 0;
}