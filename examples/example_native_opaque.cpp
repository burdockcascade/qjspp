#include <iostream>
#include <memory>
#include "qjspp.hpp"

struct Vector2D {
    double x{0};
    double y{0};
};

int main() {
    qjspp::Engine engine = qjspp::Engine::small();

    auto builder = engine.make_class<Vector2D>("Vector2D");
    qjspp::add_property_getset(builder, "x", &Vector2D::x);
    qjspp::add_property_getset(builder, "y", &Vector2D::y);
    engine.set_global("Vector2D", builder.build());

    // Register a standalone C++ function that receives a native JS object
    engine.set_global("printVector", engine.make_function([](const qjspp::CallContext& args) {
        if (args.empty()) return qjspp::Value::make_undefined(args.context());

        // Extract the raw C++ instance from the JS object
        Vector2D* vec = qjspp::get_native_opaque<Vector2D>(args[0]);
        if (vec) {
            std::cout << "Native Vector2D received: (" << vec->x << ", " << vec->y << ")\n";
        } else {
            std::cerr << "Argument is not a native Vector2D instance\n";
        }

        return qjspp::Value::make_undefined(args.context());
    }));

    engine.exec(R"(
        const v = new Vector2D();
        v.x = 42.0;
        v.y = 99.0;
        printVector(v);
    )");

    return 0;
}