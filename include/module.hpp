#pragma once

#include <quickjs.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include "value.hpp"
#include "class.hpp"

namespace qjspp {

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, std::string_view name)
            : ctx_(ctx), name_(name) {}

        // Add a Value export to the module
        void export_value(std::string_view export_name, Value val) {
            exports_.emplace_back(std::string(export_name), std::move(val));
        }

        // Add a function export using NativeFunction callback
        void export_function(std::string_view export_name, NativeFunction func) {
            export_value(export_name, Value::make_function(ctx_, std::move(func)));
        }

        void export_class(std::string_view export_name, Value val) {
            export_value(export_name, std::move(val));
        }

        // Builds and registers the ES module with QuickJS
        void finalize() {
            if (!ctx_) return;

            // Container to transfer context across module init phases safely
            struct ModuleInitContext {
                std::vector<std::pair<std::string, Value>> exports;
            };

            auto init_ctx = std::make_unique<ModuleInitContext>();
            init_ctx->exports = std::move(exports_);

            // 1. Create the native QuickJS C module
            JSModuleDef* mod = JS_NewCModule(
                ctx_,
                name_.c_str(),
                [](JSContext* ctx, JSModuleDef* m) -> int {
                    // Retrieve opaque initialization context attached to module
                    auto* init_ctx = static_cast<ModuleInitContext*>(JS_GetOpaque(JS_GetImportMeta(ctx, m), 0));

                    // Fallback to import.meta internal pointer cleanup
                    JSValue meta = JS_GetImportMeta(ctx, m);
                    JSValue ptr_val = JS_GetPropertyStr(ctx, meta, "_mod_ctx");

                    int64_t raw_ptr = 0;
                    if (JS_ToInt64(ctx, &raw_ptr, ptr_val) < 0 || raw_ptr == 0) {
                        JS_FreeValue(ctx, ptr_val);
                        JS_FreeValue(ctx, meta);
                        return -1;
                    }

                    auto* context_data = reinterpret_cast<ModuleInitContext*>(static_cast<intptr_t>(raw_ptr));
                    JS_FreeValue(ctx, ptr_val);

                    // Export values to JS engine instance
                    for (auto& [export_name, val] : context_data->exports) {
                        if (JS_SetModuleExport(ctx, m, export_name.c_str(), val.clone().release()) < 0) {
                            JS_FreeValue(ctx, meta);
                            delete context_data;
                            return -1;
                        }
                    }

                    // Remove initial pointer reference to allow cleanup post-evaluation
                    JS_DeleteProperty(ctx, meta, JS_NewAtom(ctx, "_mod_ctx"), 0);
                    JS_FreeValue(ctx, meta);
                    delete context_data;
                    return 0;
                }
            );

            if (!mod) {
                return;
            }

            // 2. Register export keys in the module declaration phase
            for (const auto& [export_name, _] : init_ctx->exports) {
                if (JS_AddModuleExport(ctx_, mod, export_name.c_str()) < 0) {
                    return;
                }
            }

            // 3. Store lifetime ownership pointer safely on import.meta
            JSValue meta = JS_GetImportMeta(ctx_, mod);
            ModuleInitContext* raw_init_ctx = init_ctx.release(); // Relinquish ownership to QuickJS context lifecycle

            JS_SetPropertyStr(ctx_, meta, "_mod_ctx", JS_NewInt64(ctx_, reinterpret_cast<intptr_t>(raw_init_ctx)));
            JS_FreeValue(ctx_, meta);
        }

    private:
        JSContext* ctx_{nullptr};
        std::string name_;
        std::vector<std::pair<std::string, Value>> exports_;
    };

} // namespace qjspp