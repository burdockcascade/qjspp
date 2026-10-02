#include "qjspp/value.hpp"

#include <stdexcept>
#include <utility>

#include "qjspp/context.hpp"

namespace qjspp {

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
    Value Value::make_string(JSContext* ctx, std::string_view str) { return {ctx, JS_NewStringLen(ctx, str.data(), str.size())}; }
    Value Value::make_object(JSContext* ctx) { return {ctx, JS_NewObject(ctx)}; }
    Value Value::make_array(JSContext* ctx) { return {ctx, JS_NewArray(ctx)}; }

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

        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int, JSValue* data) -> JSValue {
            JSClassID id = g_native_fn_class_id.load(std::memory_order_relaxed);
            auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(data[0], id));
            if (!fn || !*fn) {
                return JS_ThrowTypeError(ctx, "Native function pointer is null or invalid");
            }

            try {
                CallContext args(ctx, this_val, argc, argv);
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

    bool Value::is_undefined() const noexcept { return JS_IsUndefined(val_); }
    bool Value::is_null() const noexcept { return JS_IsNull(val_); }
    bool Value::is_bool() const noexcept { return JS_IsBool(val_); }
    bool Value::is_number() const noexcept { return JS_IsNumber(val_); }
    bool Value::is_string() const noexcept { return JS_IsString(val_); }
    bool Value::is_object() const noexcept { return JS_IsObject(val_); }
    bool Value::is_exception() const noexcept { return JS_IsException(val_); }
    bool Value::is_function() const noexcept { return JS_IsFunction(ctx_, val_); }
    bool Value::is_array() const noexcept { return JS_IsArray(val_); }

    void Value::clear_exception() const noexcept {
    if (ctx_) {
        JSValue exc = JS_GetException(ctx_);
        JS_FreeValue(ctx_, exc);
    }
}

    std::optional<bool> Value::as_bool() const noexcept {
        if (!ctx_ || is_undefined() || is_null()) return std::nullopt;
        return JS_ToBool(ctx_, val_) != 0;
    }

    std::optional<int32_t> Value::as_int() const noexcept {
        if (!ctx_) return std::nullopt;
        int32_t res = 0;
        if (JS_ToInt32(ctx_, &res, val_) < 0) {
            clear_exception();
            return std::nullopt;
        }
        return res;
    }

    std::optional<int64_t> Value::as_long() const noexcept {
        if (!ctx_) return std::nullopt;
        int64_t res = 0;
        if (JS_ToInt64(ctx_, &res, val_) < 0) {
            clear_exception();
            return std::nullopt;
        }
        return res;
    }

    std::optional<double> Value::as_double() const noexcept {
        if (!ctx_) return std::nullopt;
        double res = 0.0;
        if (JS_ToFloat64(ctx_, &res, val_) < 0) {
            clear_exception();
            return std::nullopt;
        }
        return res;
    }

    std::optional<float> Value::as_float() const noexcept {
        if (auto d = as_double()) {
            return static_cast<float>(*d);
        }
        return std::nullopt;
    }

    std::optional<std::string> Value::as_string() const noexcept {
        if (!ctx_) return std::nullopt;
        const char* str = JS_ToCString(ctx_, val_);
        if (!str) {
            clear_exception();
            return std::nullopt;
        }
        std::string result(str);
        JS_FreeCString(ctx_, str);
        return result;
    }

    std::optional<std::vector<Value>> Value::as_vector() const noexcept {
        if (!ctx_ || !is_array()) return std::nullopt;

        try {
            Value length_val = get("length");
            auto length = static_cast<uint32_t>(length_val.to_int(0));

            std::vector<Value> result;
            result.reserve(length);

            for (uint32_t i = 0; i < length; ++i) {
                result.push_back(get(i));
            }
            return result;
        } catch (...) {
            clear_exception();
            return std::nullopt;
        }
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

} // namespace qjspp