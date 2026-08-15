#include <print>
#include "engine.hpp"

int main() {
    qjspp::Engine engine;

    auto new_obj = engine.make_object();
    new_obj.set("title", engine.make_string("Developer"));
    new_obj.set("salary", engine.make_double(95000.0));

    std::println("Title: {}", new_obj.get("title").to_string());
    std::println("Salary: {}", new_obj.get("salary").to_double());

    return 0;
}