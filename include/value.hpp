#pragma once

#include <functional>
#include <quickjs.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qjspp {

    class Value;

    using NativeFunction = std::function<Value(const std::vector<Value>& args)>;

    class Value {
    public:
        Value() noexcept = default;
        Value(JSContext* ctx, JSValue val, bool dup = false) noexcept;
        ~Value();

        Value(const Value&) = delete;
        Value& operator=(const Value&) = delete;

        Value(Value&& other) noexcept;
        Value& operator=(Value&& other) noexcept;

        [[nodiscard]] Value clone() const;

        // Static Factories
        static Value make_undefined(JSContext* ctx);
        static Value make_null(JSContext* ctx);
        static Value make_bool(JSContext* ctx, bool v);
        static Value make_int(JSContext* ctx, int32_t v);
        static Value make_long(JSContext* ctx, int64_t v);
        static Value make_double(JSContext* ctx, double v);
        static Value make_string(JSContext* ctx, std::string_view str);
        static Value make_object(JSContext* ctx);
        static Value make_array(JSContext* ctx);
        static Value make_function(JSContext* ctx, NativeFunction func);

        // Checks
        [[nodiscard]] bool is_undefined() const noexcept { return JS_IsUndefined(val_); }
        [[nodiscard]] bool is_null() const noexcept { return JS_IsNull(val_); }
        [[nodiscard]] bool is_bool() const noexcept { return JS_IsBool(val_); }
        [[nodiscard]] bool is_number() const noexcept { return JS_IsNumber(val_); }
        [[nodiscard]] bool is_string() const noexcept { return JS_IsString(val_); }
        [[nodiscard]] bool is_object() const noexcept { return JS_IsObject(val_); }
        [[nodiscard]] bool is_exception() const noexcept { return JS_IsException(val_); }
        [[nodiscard]] bool is_array() const noexcept;

        // Conversions
        [[nodiscard]] bool to_bool() const;
        [[nodiscard]] int32_t to_int() const;
        [[nodiscard]] int64_t to_long() const;
        [[nodiscard]] double to_double() const;
        [[nodiscard]] std::string to_string() const;

        [[nodiscard]] Value call(std::initializer_list<Value> args) const;
        [[nodiscard]] Value call_method(const Value& this_obj, std::initializer_list<Value> args = {}) const;

        // Property Accessors
        [[nodiscard]] bool has(std::string_view key) const;
        [[nodiscard]] Value get(std::string_view key) const;
        [[nodiscard]] Value get(uint32_t index) const;
        void set(std::string_view key, const Value& val);
        void set(uint32_t index, const Value& val);

        [[nodiscard]] JSValue raw() const noexcept { return val_; }
        [[nodiscard]] JSContext* context() const noexcept { return ctx_; }
        JSValue release() noexcept;

    private:
        JSContext* ctx_{nullptr};
        JSValue val_{JS_UNDEFINED};

        void free() noexcept;
    };

    // Inline global state for native functions
    inline JSClassID g_native_fn_class_id = 0;

    // === INLINE IMPLEMENTATIONS ===

    inline Value::Value(JSContext* ctx, JSValue val, bool dup) noexcept
        : ctx_(ctx), val_(val) {
        if (dup && ctx_ && !JS_IsUndefined(val_)) {
            JS_DupValue(ctx_, val_);
        }
    }

    inline Value::~Value() {
        free();
    }

    inline Value::Value(Value&& other) noexcept
        : ctx_(std::exchange(other.ctx_, nullptr)),
          val_(std::exchange(other.val_, JS_UNDEFINED)) {}

    inline Value& Value::operator=(Value&& other) noexcept {
        if (this != &other) {
            free();
            ctx_ = std::exchange(other.ctx_, nullptr);
            val_ = std::exchange(other.val_, JS_UNDEFINED);
        }
        return *this;
    }

    inline Value Value::clone() const {
        if (!ctx_) return {};
        return {ctx_, val_, true};
    }

    // === MAKE VALUES ===

    inline Value Value::make_undefined(JSContext* ctx) {
        return {ctx, JS_UNDEFINED};
    }

    inline Value Value::make_null(JSContext* ctx) {
        return {ctx, JS_NULL};
    }

    inline Value Value::make_bool(JSContext* ctx, bool v) {
        return {ctx, JS_NewBool(ctx, v)};
    }

    inline Value Value::make_int(JSContext* ctx, int32_t v) {
        return {ctx, JS_NewInt32(ctx, v)};
    }

    inline Value Value::make_long(JSContext* ctx, int64_t v) {
        return {ctx, JS_NewInt64(ctx, v)};
    }

    inline Value Value::make_double(JSContext* ctx, double v) {
        return {ctx, JS_NewFloat64(ctx, v)};
    }

    inline Value Value::make_string(JSContext* ctx, std::string_view str) {
        return {ctx, JS_NewStringLen(ctx, str.data(), str.size())};
    }

    inline Value Value::make_object(JSContext* ctx) {
        if (!ctx) return {};
        return {ctx, JS_NewObject(ctx)};
    }

    inline Value Value::make_array(JSContext* ctx) {
        if (!ctx) return {};
        return {ctx, JS_NewArray(ctx)};
    }

    inline Value Value::make_function(JSContext* ctx, NativeFunction func) {
        if (!ctx) return {};

        JSRuntime* rt = JS_GetRuntime(ctx);

        if (g_native_fn_class_id == 0) {
            JS_NewClassID(rt, &g_native_fn_class_id);

            JSClassDef class_def{};
            class_def.class_name = "CppNativeFunction";
            class_def.finalizer = [](JSRuntime* rt, JSValue val) {
                auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(val, g_native_fn_class_id));
                delete fn;
            };

            JS_NewClass(rt, g_native_fn_class_id, &class_def);
        }

        auto* fn_ptr = new NativeFunction(std::move(func));

        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(data[0], g_native_fn_class_id));
            if (!fn || !*fn) {
                return JS_ThrowTypeError(ctx, "Native function pointer is null or invalid");
            }

            std::vector<Value> args;
            args.reserve(argc);
            for (int i = 0; i < argc; ++i) {
                args.emplace_back(ctx, argv[i], /*dup=*/true);
            }

            try {
                Value result = (*fn)(args);
                return result.release();
            } catch (const std::exception& e) {
                return JS_ThrowTypeError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowTypeError(ctx, "Unknown exception in native callback");
            }
        };

        JSValue opaque_val = JS_NewObjectClass(ctx, g_native_fn_class_id);
        JS_SetOpaque(opaque_val, fn_ptr);

        JSValue func_val = JS_NewCFunctionData(ctx, trampoline, 0, 0, 1, &opaque_val);
        JS_FreeValue(ctx, opaque_val);

        return {ctx, func_val, false};
    }

    // === IS / CONVERT VALUE ===

    inline bool Value::is_array() const noexcept {
        return ctx_ && JS_IsArray(val_);
    }

    inline bool Value::to_bool() const {
        return JS_ToBool(ctx_, val_);
    }

    inline int32_t Value::to_int() const {
        int32_t res = 0;
        if (JS_ToInt32(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to int");
        }
        return res;
    }

    inline int64_t Value::to_long() const {
        int64_t res = 0;
        if (JS_ToInt64(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to long");
        }
        return res;
    }

    inline double Value::to_double() const {
        double res = 0.0;
        if (JS_ToFloat64(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to double");
        }
        return res;
    }

    inline std::string Value::to_string() const {
        const char* str = JS_ToCString(ctx_, val_);
        if (!str) {
            throw std::runtime_error("Failed converting JSValue to CString");
        }
        std::string result(str);
        JS_FreeCString(ctx_, str);
        return result;
    }

    inline bool Value::has(std::string_view key) const {
        if (!ctx_ || !is_object()) return false;

        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());
        int res = JS_HasProperty(ctx_, val_, atom);
        JS_FreeAtom(ctx_, atom);

        return res > 0;
    }

    inline Value Value::get(std::string_view key) const {
        if (!ctx_) {
            throw std::runtime_error("Cannot get property: JSContext is null");
        }

        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());
        JSValue prop_raw = JS_GetProperty(ctx_, val_, atom);
        JS_FreeAtom(ctx_, atom);

        Value prop(ctx_, prop_raw, /*dup=*/false);
        if (prop.is_exception()) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
            throw std::runtime_error("JS Get Property Error: " + exc_val.to_string());
        }

        return prop;
    }

    inline Value Value::get(uint32_t index) const {
        if (!ctx_) {
            throw std::runtime_error("Cannot get indexed property: JSContext is null");
        }

        JSValue prop_raw = JS_GetPropertyUint32(ctx_, val_, index);
        Value prop(ctx_, prop_raw, /*dup=*/false);

        if (prop.is_exception()) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
            throw std::runtime_error("JS Get Index Error: " + exc_val.to_string());
        }

        return prop;
    }

    inline void Value::set(std::string_view key, const Value& val) {
        if (!ctx_) {
            throw std::runtime_error("Cannot set property: JSContext is null");
        }

        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());
        Value val_copy = val.clone();
        int res = JS_SetProperty(ctx_, val_, atom, val_copy.release());
        JS_FreeAtom(ctx_, atom);

        if (res < 0) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
            throw std::runtime_error("JS Set Property Error: " + exc_val.to_string());
        }
    }

    inline void Value::set(uint32_t index, const Value& val) {
        if (!ctx_) {
            throw std::runtime_error("Cannot set indexed property: JSContext is null");
        }

        Value val_copy = val.clone();
        int res = JS_SetPropertyUint32(ctx_, val_, index, val_copy.release());

        if (res < 0) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
            throw std::runtime_error("JS Set Index Error: " + exc_val.to_string());
        }
    }

    inline Value Value::call(std::initializer_list<Value> args) const {
        Value undefined_this(ctx_, JS_UNDEFINED);
        return call_method(undefined_this, args);
    }

    inline Value Value::call_method(const Value& this_obj, std::initializer_list<Value> args) const {
        if (!ctx_) {
            throw std::runtime_error("Cannot call function: JSContext is null");
        }

        std::vector<JSValueConst> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) {
            raw_args.push_back(arg.raw());
        }

        JSValue result_raw = JS_Call(
            ctx_,
            val_,
            this_obj.raw(),
            static_cast<int>(raw_args.size()),
            raw_args.data()
        );

        Value result(ctx_, result_raw, false);

        if (result.is_exception()) {
            Value exc_val(ctx_, JS_GetException(ctx_), false);
            std::string err_msg;
            try {
                err_msg = exc_val.to_string();
            } catch (...) {
                err_msg = "Unknown error occurred during JS function execution";
            }
            throw std::runtime_error("JS Call Error: " + err_msg);
        }

        return result;
    }

    inline JSValue Value::release() noexcept {
        JSValue val = val_;
        val_ = JS_UNDEFINED;
        ctx_ = nullptr;
        return val;
    }

    inline void Value::free() noexcept {
        if (ctx_ && !JS_IsUndefined(val_)) {
            JS_FreeValue(ctx_, val_);
            val_ = JS_UNDEFINED;
        }
    }

} // namespace qjspp