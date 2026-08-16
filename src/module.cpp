#include <qjspp.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace qjspp {

    struct ModuleInitContext {
        std::vector<std::pair<std::string, Value>> exports;
    };

    void ModuleBuilder::finalize() {
        if (!ctx_) return;

        auto init_ctx = std::make_unique<ModuleInitContext>();
        init_ctx->exports = std::move(exports_);

        JSModuleDef* mod = JS_NewCModule(
            ctx_,
            name_.c_str(),
            [](JSContext* ctx, JSModuleDef* m) -> int {
                auto* init_ctx = static_cast<ModuleInitContext*>(JS_GetOpaque(JS_GetImportMeta(ctx, m), 0));

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

                for (auto& [export_name, val] : context_data->exports) {
                    if (JS_SetModuleExport(ctx, m, export_name.c_str(), val.clone().release()) < 0) {
                        JS_FreeValue(ctx, meta);
                        delete context_data;
                        return -1;
                    }
                }

                JS_DeleteProperty(ctx, meta, JS_NewAtom(ctx, "_mod_ctx"), 0);
                JS_FreeValue(ctx, meta);
                delete context_data;
                return 0;
            }
        );

        if (!mod) return;

        for (const auto& [export_name, _] : init_ctx->exports) {
            if (JS_AddModuleExport(ctx_, mod, export_name.c_str()) < 0) return;
        }

        JSValue meta = JS_GetImportMeta(ctx_, mod);
        ModuleInitContext* raw_init_ctx = init_ctx.release();
        JS_SetPropertyStr(ctx_, meta, "_mod_ctx", JS_NewInt64(ctx_, reinterpret_cast<intptr_t>(raw_init_ctx)));
        JS_FreeValue(ctx_, meta);
    }

} // namespace qjspp