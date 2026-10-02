#include <iostream>
#include <memory>
#include <string>
#include "qjspp.hpp"

class Player {
public:
    Player(std::string name, int health = 100)
        : name_(std::move(name)), health_(health) {}

    void take_damage(int amount) {
        health_ = std::max(0, health_ - amount);
    }

    [[nodiscard]] bool is_alive() const {
        return health_ > 0;
    }

    std::string name_;
    int health_;
};

int main() {

    // 1. Initialize the QuickJS engine with medium memory usage
    qjspp::Engine engine = qjspp::Engine::medium();

    // 2. Create the ClassBuilder
    auto builder = engine.make_class<Player>("Player");

    // 3. Define constructor: new Player(name, [health])
    builder.constructor([](const qjspp::CallContext& args) {
        std::string name = args.size() > 0 ? args[0].to_string("Unknown") : "Unknown";
        int health = args.size() > 1 ? args[1].to_int(100) : 100;
        return std::make_unique<Player>(std::move(name), health);
    });

    // 4. Define instance methods
    builder.instance_method("takeDamage", [](Player* self, const qjspp::CallContext& args) {
        int amount = args.size() > 0 ? args[0].to_int(0) : 0;
        self->take_damage(amount);
        return qjspp::Value::make_undefined(args.context());
    });

    builder.instance_method("isAlive", [](Player* self, const qjspp::CallContext& args) {
        return qjspp::Value::make_bool(args.context(), self->is_alive());
    });

    // 5. Define properties with manual getters and setters
    builder.property(
        "name",
        [](JSContext* ctx, Player* self) {
            return qjspp::Value::make_string(ctx, self->name_);
        },
        [](Player* self, const qjspp::Value& val) {
            self->name_ = val.to_string();
        }
    );

    builder.property(
        "health",
        [](JSContext* ctx, Player* self) {
            return qjspp::Value::make_int(ctx, self->health_);
        },
        [](Player* self, const qjspp::Value& val) {
            self->health_ = val.to_int();
        }
    );

    // 6. Define static methods: Player.createDefault()
    builder.static_method("createDefault", [](const qjspp::CallContext& args) {
        auto player = std::make_unique<Player>("NPC", 50);
        return qjspp::Value::make_native_object(args.context(), std::move(player));
    });

    // 7. Build the class and expose it globally
    engine.set_global("Player", builder.build());

    // 8. Execute JavaScript using the bound class
    const char* js_code = R"(
        const hero = new Player("Arthur", 120);
        hero.takeDamage(30);

        const npc = Player.createDefault();

        `Hero: ${hero.name}, HP: ${hero.health}, Alive: ${hero.isAlive()} | NPC: ${npc.name}, HP: ${npc.health}`;
    )";

    auto result = engine.eval(js_code);
    if (result) {
        std::cout << result->to_string() << "\n";
        // Output: Hero: Arthur, HP: 90, Alive: true | NPC: NPC, HP: 50
    } else {
        std::cerr << "JS Error: " << result.error().to_string() << "\n";
    }

    return 0;
}