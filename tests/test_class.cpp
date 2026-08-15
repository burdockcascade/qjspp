#include <memory>
#include <cmath>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <qjspp.hpp>

class TestPoint {
public:
    double x;
    double y;

    TestPoint(double x, double y) : x(x), y(y) {}

    [[nodiscard]] double distance_to(const TestPoint& other) const {
        double dx = x - other.x;
        double dy = y - other.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    void offset(double dx, double dy) {
        x += dx;
        y += dy;
    }
};

TEST_CASE("ClassBuilder - Registration and Instantiation", "[class_builder]") {
    qjspp::Engine engine = qjspp::Engine::micro();
    qjspp::Value global = engine.global();

    qjspp::Value point = engine.make_class<TestPoint>("Point")
        .constructor([](const std::vector<qjspp::Value>& args) {
            double x = args.size() > 0 ? args[0].to_double() : 0.0;
            double y = args.size() > 1 ? args[1].to_double() : 0.0;
            return std::make_unique<TestPoint>(x, y);
        })
        .instance_method("offset", [](TestPoint* self, const std::vector<qjspp::Value>& args) {
            double dx = args.size() > 0 ? args[0].to_double() : 0.0;
            double dy = args.size() > 1 ? args[1].to_double() : 0.0;
            self->offset(dx, dy);
            return qjspp::Value::make_undefined(args[0].context());
        })
        .instance_method("distanceTo", [](TestPoint* self, const std::vector<qjspp::Value>& args) {
            if (args.empty()) {
                throw std::runtime_error("Expected a Point argument");
            }
            auto* other = qjspp::get_native_opaque<TestPoint>(args[0]);
            if (!other) {
                throw std::runtime_error("Argument must be an instance of Point");
            }
            return qjspp::Value::make_double(args[0].context(), self->distance_to(*other));
        })
        .build();

    global.set("Point", point);

    SECTION("Instantiate object via JavaScript constructor") {
        qjspp::Value result = engine.eval("const p = new Point(10, 20); p;").value();
        REQUIRE(result.is_object());

        auto* native_ptr = qjspp::get_native_opaque<TestPoint>(result);
        REQUIRE(native_ptr != nullptr);
        CHECK(native_ptr->x == 10.0);
        CHECK(native_ptr->y == 20.0);
    }

    SECTION("Invoke native instance methods from JavaScript") {
        std::ignore = engine.eval(R"(
            const p = new Point(5, 5);
            p.offset(2, 3);
        )");

        qjspp::Value p_val = engine.eval("p").value();
        auto* native_ptr = qjspp::get_native_opaque<TestPoint>(p_val);
        REQUIRE(native_ptr != nullptr);
        CHECK(native_ptr->x == 7.0);
        CHECK(native_ptr->y == 8.0);
    }

    SECTION("Pass native object instance as method argument") {
        qjspp::Value dist_val = engine.eval(R"(
            const p1 = new Point(0, 0);
            const p2 = new Point(3, 4);
            p1.distanceTo(p2);
        )").value();

        CHECK(dist_val.to_double() == 5.0);
    }

    SECTION("Throw exception when passing invalid object type") {
        REQUIRE_THROWS_WITH(
            engine.exec(R"(
                const p1 = new Point(0, 0);
                p1.distanceTo({ x: 3, y: 4 }); // Plain object, not a native Point
            )"),
            Catch::Matchers::ContainsSubstring("Argument must be an instance of Point")
        );
    }
}

TEST_CASE("ClassBuilder - Properties", "[class_builder]") {
    qjspp::Engine engine = qjspp::Engine::micro();
    qjspp::Value global = engine.global();

    qjspp::Value point = engine.make_class<TestPoint>("Point")
        .constructor([](const std::vector<qjspp::Value>& args) {
            double x = args.size() > 0 ? args[0].to_double() : 0.0;
            double y = args.size() > 1 ? args[1].to_double() : 0.0;
            return std::make_unique<TestPoint>(x, y);
        })
        // Readable & Writable property for 'x'
        .property("x",
            [](JSContext* ctx, TestPoint* self) {
                return qjspp::Value::make_double(ctx, self->x);
            },
            [](TestPoint* self, const qjspp::Value& val) {
                self->x = val.to_double();
            }
        )
        // Read-only computed property
        .property("magnitude",
            [](JSContext* ctx, TestPoint* self) {
                double mag = std::sqrt(self->x * self->x + self->y * self->y);
                return qjspp::Value::make_double(ctx, mag);
            } // No setter provided
        )
        .build();

    global.set("Point", point);

    SECTION("Read and write via property accessors") {
        std::ignore = engine.eval(R"(
            const p = new Point(10, 20);
            p.x = 30; // Triggers setter
        )");

        qjspp::Value p_val = engine.eval("p").value();
        auto* native_ptr = qjspp::get_native_opaque<TestPoint>(p_val);
        REQUIRE(native_ptr != nullptr);

        // Ensure C++ state was updated by the setter
        CHECK(native_ptr->x == 30.0);

        // Ensure JS getter returns the right value
        qjspp::Value x_val = engine.eval("p.x").value();
        CHECK(x_val.to_double() == 30.0);
    }

    SECTION("Access read-only computed property") {
        qjspp::Value mag_val = engine.eval(R"(
            const p = new Point(3, 4);
            p.magnitude; // Triggers getter
        )").value();

        CHECK(mag_val.to_double() == 5.0);
    }
}

TEST_CASE("ClassBuilder - Static Methods", "[class_builder]") {
    qjspp::Engine engine = qjspp::Engine::micro();
    qjspp::Value global = engine.global();

    qjspp::Value point = engine.make_class<TestPoint>("Point")
        .constructor([](const std::vector<qjspp::Value>& args) {
            double x = args.size() > 0 ? args[0].to_double() : 0.0;
            double y = args.size() > 1 ? args[1].to_double() : 0.0;
            return std::make_unique<TestPoint>(x, y);
        })
        .static_method("fromPolar", [](const std::vector<qjspp::Value>& args) {
            double r = args.size() > 0 ? args[0].to_double() : 0.0;
            double theta = args.size() > 1 ? args[1].to_double() : 0.0;
            auto pt = std::make_unique<TestPoint>(r * std::cos(theta), r * std::sin(theta));
            return qjspp::make_native_object(args[0].context(), std::move(pt));
        })
        .build();

    global.set("Point", point);

    SECTION("Call static factory method from JS") {
        qjspp::Value result = engine.eval("Point.fromPolar(10, 0);").value();
        REQUIRE(result.is_object());

        auto* native_ptr = qjspp::get_native_opaque<TestPoint>(result);
        REQUIRE(native_ptr != nullptr);
        CHECK(native_ptr->x == 10.0);
        CHECK(native_ptr->y == 0.0);
    }
}