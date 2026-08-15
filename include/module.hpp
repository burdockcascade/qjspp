#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "value.hpp"
#include "class.hpp"

namespace qjspp {

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, std::string_view name)
            : ctx_(ctx), name_(name) {}

        // Add a Value export to the module
        ModuleBuilder& export_value(std::string_view export_name, Value val) {
            exports_.emplace_back(std::string(export_name), std::move(val));
            return *this;
        }

        // Add a function export using NativeFunction callback
        ModuleBuilder& export_function(std::string_view export_name, NativeFunction func) {
            return export_value(export_name, Value::make_function(ctx_, std::move(func)));
        }

        ModuleBuilder& export_class(std::string_view export_name, Value val) {
            return export_value(export_name, std::move(val));
        }

        // Builds and registers the ES module with QuickJS
        JSModuleDef* build() {
            if (!ctx_) return nullptr;

            // Move instance storage to heap so it remains valid during module evaluation
            auto* self = new ModuleBuilder(std::move(*this));

            // 1. Create the native QuickJS C module using the init callback
            JSModuleDef* mod = JS_NewCModule(
                ctx_,
                self->name_.c_str(),
                [](JSContext* ctx, JSModuleDef* m) -> int {
                    // Extract the heap pointer stored on import.meta using property access
                    JSValue meta = JS_GetImportMeta(ctx, m);
                    JSValue builder_val = JS_GetPropertyStr(ctx, meta, "_builder");

                    int64_t ptr_val = 0;
                    JS_ToInt64(ctx, &ptr_val, builder_val);
                    auto* builder = reinterpret_cast<ModuleBuilder*>(static_cast<intptr_t>(ptr_val));

                    JS_FreeValue(ctx, builder_val);
                    JS_FreeValue(ctx, meta);

                    if (!builder) return -1;

                    // Export each value to the JS context module definition
                    for (auto& [export_name, val] : builder->exports_) {
                        // JS_SetModuleExport consumes reference count; pass clone release
                        JS_SetModuleExport(ctx, m, export_name.c_str(), val.clone().release());
                    }

                    delete builder; // Cleanup heap state post-initialization
                    return 0;
                }
            );

            if (!mod) {
                delete self;
                return nullptr;
            }

            // 2. Safely attach the builder state pointer to import.meta as a numeric property
            JSValue meta = JS_GetImportMeta(ctx_, mod);
            JS_SetPropertyStr(ctx_, meta, "_builder", JS_NewInt64(ctx_, reinterpret_cast<intptr_t>(self)));
            JS_FreeValue(ctx_, meta);

            // 3. Register export keys in the module declaration phase
            for (const auto& [export_name, _] : self->exports_) {
                JS_AddModuleExport(ctx_, mod, export_name.c_str());
            }

            return mod;
        }

    private:
        JSContext* ctx_{nullptr};
        std::string name_;
        std::vector<std::pair<std::string, Value>> exports_;
    };

} // namespace qjspp