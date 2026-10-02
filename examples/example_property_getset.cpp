#include <iostream>
#include <memory>
#include <string>
#include "qjspp.hpp"

struct Transform {
    double x = 0.0;
    double y = 0.0;
    bool visible = true;
    std::string tag = "Entity";
};

int main() {
    qjspp::Engine engine = qjspp::Engine::small();

    auto builder = engine.make_class<Transform>("Transform");

    // Constructor: new Transform(x, y)
    builder.constructor([](const qjspp::CallContext& args) {
        auto t = std::make_unique<Transform>();
        if (args.size() > 0) t->x = args[0].to_double();
        if (args.size() > 1) t->y = args[1].to_double();
        return t;
    });

    // Automatically bind member variables as properties
    qjspp::add_property_getset(builder, "x", &Transform::x);
    qjspp::add_property_getset(builder, "y", &Transform::y);
    qjspp::add_property_getset(builder, "visible", &Transform::visible);
    qjspp::add_property_getset(builder, "tag", &Transform::tag);

    engine.set_global("Transform", builder.build());

    const char* script = R"(
        const t = new Transform(10.5, 20.25);
        t.visible = false;
        t.tag = "PlayerTarget";

        // Access and modify properties
        t.x += 5.0;
        `Tag: ${t.tag}, Visible: ${t.visible}, Pos: (${t.x}, ${t.y})`;
    )";

    auto res = engine.eval(script);
    if (res) {
        std::cout << res->to_string() << "\n";
        // Output: Tag: PlayerTarget, Visible: false, Pos: (15.5, 20.25)
    }

    return 0;
}