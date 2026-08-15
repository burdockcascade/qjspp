#include <qjspp.hpp>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace qjspp {

    // ==========================================
    // engine.hpp implementations
    // ==========================================

    std::string read_file_content(const std::filesystem::path& filepath) {
        std::ifstream file(filepath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath.string());
        }

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 0) {
            throw std::runtime_error("Failed to determine size of file: " + filepath.string());
        }
        file.seekg(0, std::ios::beg);

        std::string content;
        content.resize_and_overwrite(size, [&file](char* buf, size_t n) {
            file.read(buf, n);
            return file.gcount();
        });

        return content;
    }

    Engine::Engine() : Engine(0, 0) {}

    Engine::Engine(const size_t memory_limit, const size_t stack_size) {
        rt_ = JS_NewRuntime();
        if (!rt_) {
            throw std::runtime_error("Failed to create QuickJS Runtime");
        }

        if (memory_limit > 0) {
            JS_SetMemoryLimit(rt_, memory_limit);
        }
        if (stack_size > 0) {
            JS_SetMaxStackSize(rt_, stack_size);
        }

        ctx_ = JS_NewContext(rt_);
        if (!ctx_) {
            JS_FreeRuntime(rt_);
            rt_ = nullptr;
            throw std::runtime_error("Failed to create QuickJS Context");
        }
    }

    Engine::~Engine() {
        if (ctx_) {
            JS_FreeContext(ctx_);
            ctx_ = nullptr;
        }
        if (rt_) {
            JS_FreeRuntime(rt_);
            rt_ = nullptr;
        }
    }

    Engine::Engine(Engine&& other) noexcept
        : rt_(std::exchange(other.rt_, nullptr)),
          ctx_(std::exchange(other.ctx_, nullptr)) {}

    Engine& Engine::operator=(Engine&& other) noexcept {
        if (this != &other) {
            if (ctx_) JS_FreeContext(ctx_);
            if (rt_) JS_FreeRuntime(rt_);

            rt_ = std::exchange(other.rt_, nullptr);
            ctx_ = std::exchange(other.ctx_, nullptr);
        }
        return *this;
    }

    void Engine::gc() const {
        if (rt_) {
            JS_RunGC(rt_);
        }
    }

    std::string Engine::format_exception() const {
        return get_and_clear_exception().to_string();
    }

    void Engine::check_exception(const Value& val) const {
        if (val.is_exception()) {
            throw std::runtime_error(format_exception());
        }
    }

    JsError Engine::get_and_clear_exception() const {
        Value exception_val(ctx_, JS_GetException(ctx_), /*dup=*/false);

        JsError err;
        try {
            err.message = exception_val.to_string();
        } catch (...) {
            err.message = "Unknown JavaScript Error";
        }

        if (exception_val.is_object()) {
            if (exception_val.has("stack")) {
                try {
                    Value stack_val = exception_val.get("stack");
                    if (!stack_val.is_undefined() && !stack_val.is_null()) {
                        err.stack = stack_val.to_string();
                    }
                } catch (...) {}
            }

            if (exception_val.has("fileName")) {
                try {
                    Value file_val = exception_val.get("fileName");
                    if (file_val.is_string()) {
                        err.filename = file_val.to_string();
                    }
                } catch (...) {}
            }

            if (exception_val.has("lineNumber")) {
                try {
                    Value line_val = exception_val.get("lineNumber");
                    if (line_val.is_number()) {
                        err.line_number = line_val.to_int();
                    }
                } catch (...) {}
            }
        }

        return err;
    }

    std::expected<Value, JsError> Engine::eval(std::string_view code, const char* filename, int eval_flags) const {
        Value result(
            ctx_,
            JS_Eval(ctx_, code.data(), code.size(), filename, eval_flags),
            false
        );

        if (result.is_exception()) {
            return std::unexpected(get_and_clear_exception());
        }

        return result;
    }

    void Engine::exec(std::string_view code, const char* filename, int eval_flags) const {
        Value result(
            ctx_,
            JS_Eval(ctx_, code.data(), code.size(), filename, eval_flags),
            false
        );

        check_exception(result);
    }

    std::expected<Value, JsError> Engine::eval_file(const std::filesystem::path& filepath, int eval_flags) const {
        try {
            std::string code = read_file_content(filepath);
            return eval(code, filepath.string().c_str(), eval_flags);
        } catch (const std::exception& e) {
            JsError err;
            err.message = e.what();
            err.filename = filepath.string();
            return std::unexpected(err);
        }
    }

    void Engine::exec_file(const std::filesystem::path& filepath, int eval_flags) const {
        std::string code = read_file_content(filepath);
        exec(code, filepath.string().c_str(), eval_flags);
    }

    // ==========================================
    // value.hpp implementations
    // ==========================================

    Value::Value(JSContext* ctx, JSValue val, bool dup) noexcept
        : ctx_(ctx), val_(val) {
        if (dup && ctx_ && !JS_IsUndefined(val_)) {
            JS_DupValue(ctx_, val_);
        }
    }

    Value::~Value() {
        free();
    }

    Value::Value(Value&& other) noexcept
        : ctx_(std::exchange(other.ctx_, nullptr)),
          val_(std::exchange(other.val_, JS_UNDEFINED)) {}

    Value& Value::operator=(Value&& other) noexcept {
        if (this != &other) {
            free();
            ctx_ = std::exchange(other.ctx_, nullptr);
            val_ = std::exchange(other.val_, JS_UNDEFINED);
        }
        return *this;
    }

    Value Value::clone() const {
        if (!ctx_) return {};
        return {ctx_, val_, true};
    }

    std::string Value::fetch_and_clear_exception() const {
        if (!ctx_) return "Unknown JS Exception (Null Context)";
        JSValue exc = JS_GetException(ctx_);
        Value exc_val(ctx_, exc, false);
        try {
            return exc_val.to_string();
        } catch (...) {
            return "Unknown Exception";
        }
    }

    Value Value::make_undefined(JSContext* ctx) { return {ctx, JS_UNDEFINED}; }
    Value Value::make_null(JSContext* ctx) { return {ctx, JS_NULL}; }
    Value Value::make_bool(JSContext* ctx, bool v) { return {ctx, JS_NewBool(ctx, v)}; }
    Value Value::make_int(JSContext* ctx, int32_t v) { return {ctx, JS_NewInt32(ctx, v)}; }
    Value Value::make_long(JSContext* ctx, int64_t v) { return {ctx, JS_NewInt64(ctx, v)}; }
    Value Value::make_double(JSContext* ctx, double v) { return {ctx, JS_NewFloat64(ctx, v)}; }
    Value Value::make_string(JSContext* ctx, std::string_view str) {
        return {ctx, JS_NewStringLen(ctx, str.data(), str.size())};
    }
    Value Value::make_object(JSContext* ctx) {
        if (!ctx) return {};
        return {ctx, JS_NewObject(ctx)};
    }
    Value Value::make_array(JSContext* ctx) {
        if (!ctx) return {};
        return {ctx, JS_NewArray(ctx)};
    }

    Value Value::make_function(JSContext* ctx, NativeFunction func) {
        if (!ctx) return {};

        JSRuntime* rt = JS_GetRuntime(ctx);
        JSClassID class_id = g_native_fn_class_id.load(std::memory_order_relaxed);

        if (class_id == 0) {
            JS_NewClassID(rt, &class_id);
            g_native_fn_class_id.store(class_id, std::memory_order_relaxed);
        }

        if (!JS_IsRegisteredClass(rt, class_id)) {
            JSClassDef class_def{};
            class_def.class_name = "CppNativeFunction";
            class_def.finalizer = [](JSRuntime*, JSValue val) {
                JSClassID id = g_native_fn_class_id.load(std::memory_order_relaxed);
                auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(val, id));
                delete fn;
            };

            JS_NewClass(rt, class_id, &class_def);
        }

        auto* fn_ptr = new NativeFunction(std::move(func));

        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            JSClassID id = g_native_fn_class_id.load(std::memory_order_relaxed);
            auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(data[0], id));
            if (!fn || !*fn) {
                return JS_ThrowTypeError(ctx, "Native function pointer is null or invalid");
            }

            try {
                ArgList args(ctx, argc, argv);
                Value result = (*fn)(args);
                return result.release();
            } catch (const std::exception& e) {
                return JS_ThrowTypeError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowTypeError(ctx, "Unknown exception in native callback");
            }
        };

        JSValue opaque_val = JS_NewObjectClass(ctx, class_id);
        JS_SetOpaque(opaque_val, fn_ptr);

        JSValue func_val = JS_NewCFunctionData(ctx, trampoline, 0, 0, 1, &opaque_val);
        JS_FreeValue(ctx, opaque_val);

        return {ctx, func_val, false};
    }

    bool Value::is_array() const noexcept { return ctx_ && JS_IsArray(val_); }

    bool Value::to_bool() const { return JS_ToBool(ctx_, val_); }

    int32_t Value::to_int() const {
        int32_t res = 0;
        if (JS_ToInt32(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to int: " + fetch_and_clear_exception());
        }
        return res;
    }

    int64_t Value::to_long() const {
        int64_t res = 0;
        if (JS_ToInt64(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to long: " + fetch_and_clear_exception());
        }
        return res;
    }

    double Value::to_double() const {
        double res = 0.0;
        if (JS_ToFloat64(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to double: " + fetch_and_clear_exception());
        }
        return res;
    }

    std::string Value::to_string() const {
        if (!ctx_) return "";
        const char* str = JS_ToCString(ctx_, val_);
        if (!str) {
            throw std::runtime_error("Failed converting JSValue to CString: " + fetch_and_clear_exception());
        }
        std::string result(str);
        JS_FreeCString(ctx_, str);
        return result;
    }

    bool Value::has(std::string_view key) const {
        if (!ctx_ || !is_object()) return false;
        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());
        int res = JS_HasProperty(ctx_, val_, atom);
        JS_FreeAtom(ctx_, atom);
        return res > 0;
    }

    Value Value::get(std::string_view key) const {
        if (!ctx_) throw std::runtime_error("Cannot get property: JSContext is null");
        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());
        JSValue prop_raw = JS_GetProperty(ctx_, val_, atom);
        JS_FreeAtom(ctx_, atom);
        Value prop(ctx_, prop_raw, false);
        if (prop.is_exception()) throw std::runtime_error("JS Get Property Error: " + fetch_and_clear_exception());
        return prop;
    }

    Value Value::get(uint32_t index) const {
        if (!ctx_) throw std::runtime_error("Cannot get indexed property: JSContext is null");
        JSValue prop_raw = JS_GetPropertyUint32(ctx_, val_, index);
        Value prop(ctx_, prop_raw, false);
        if (prop.is_exception()) throw std::runtime_error("JS Get Index Error: " + fetch_and_clear_exception());
        return prop;
    }

    void Value::set(std::string_view key, const Value& val) {
        if (!ctx_) throw std::runtime_error("Cannot set property: JSContext is null");
        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());
        Value val_copy = val.clone();
        int res = JS_SetProperty(ctx_, val_, atom, val_copy.release());
        JS_FreeAtom(ctx_, atom);
        if (res < 0) throw std::runtime_error("JS Set Property Error: " + fetch_and_clear_exception());
    }

    void Value::set(uint32_t index, const Value& val) {
        if (!ctx_) throw std::runtime_error("Cannot set indexed property: JSContext is null");
        Value val_copy = val.clone();
        int res = JS_SetPropertyUint32(ctx_, val_, index, val_copy.release());
        if (res < 0) throw std::runtime_error("JS Set Index Error: " + fetch_and_clear_exception());
    }

    Value Value::call(std::initializer_list<Value> args) const {
        Value undefined_this(ctx_, JS_UNDEFINED);
        return call_method(undefined_this, args);
    }

    Value Value::call_method(const Value& this_obj, std::initializer_list<Value> args) const {
        if (!ctx_) throw std::runtime_error("Cannot call function: JSContext is null");
        std::vector<JSValueConst> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) raw_args.push_back(arg.raw());

        JSValue result_raw = JS_Call(ctx_, val_, this_obj.raw(), static_cast<int>(raw_args.size()), raw_args.data());
        Value result(ctx_, result_raw, false);
        if (result.is_exception()) throw std::runtime_error("JS Call Error: " + fetch_and_clear_exception());
        return result;
    }

    JSValue Value::release() noexcept {
        JSValue val = val_;
        val_ = JS_UNDEFINED;
        ctx_ = nullptr;
        return val;
    }

    void Value::free() noexcept {
        if (ctx_ && !JS_IsUndefined(val_)) {
            JS_FreeValue(ctx_, val_);
            val_ = JS_UNDEFINED;
        }
    }

    Value ArgList::operator[](size_t index) const {
        if (index >= static_cast<size_t>(argc_)) return Value::make_undefined(ctx_);
        return {ctx_, argv_[index], true};
    }

    // ==========================================
    // module.hpp implementations
    // ==========================================

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