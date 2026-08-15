#include "value.hpp"
#include <utility>
#include <stdexcept>
#include <vector>

namespace qjspp {

    Value::Value() noexcept = default;

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

    Value Value::make_undefined(JSContext* ctx) {
        return {ctx, JS_UNDEFINED};
    }

    Value Value::make_null(JSContext* ctx) {
        return {ctx, JS_NULL};
    }

    Value Value::make_bool(JSContext* ctx, bool v) {
        return {ctx, JS_NewBool(ctx, v)};
    }

    Value Value::make_int32(JSContext* ctx, int32_t v) {
        return {ctx, JS_NewInt32(ctx, v)};
    }

    Value Value::make_double(JSContext* ctx, double v) {
        return {ctx, JS_NewFloat64(ctx, v)};
    }

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

    static JSClassID g_native_fn_class_id = 0;

    Value Value::make_function(JSContext* ctx, NativeFunction func) {
        if (!ctx) return Value();

        JSRuntime* rt = JS_GetRuntime(ctx);

        // 1. Ensure a valid JSClassID with a destructor/finalizer is registered ONCE
        if (g_native_fn_class_id == 0) {
            JS_NewClassID(rt, &g_native_fn_class_id);

            JSClassDef class_def{};
            class_def.class_name = "CppNativeFunction";
            class_def.finalizer = [](JSRuntime* rt, JSValue val) {
                // Free the heap-allocated std::function when QuickJS GCs this object
                auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(val, g_native_fn_class_id));
                delete fn;
            };

            JS_NewClass(rt, g_native_fn_class_id, &class_def);
        }

        // 2. Heap-allocate the std::function payload
        auto* fn_ptr = new NativeFunction(std::move(func));

        // 3. Define the C-style trampoline matching JSCFunctionData signature
        auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
            // Retrieve the captured C++ std::function safely using our ClassID
            auto* fn = static_cast<NativeFunction*>(JS_GetOpaque(data[0], g_native_fn_class_id));
            if (!fn || !*fn) {
                return JS_ThrowTypeError(ctx, "Native function pointer is null or invalid");
            }

            // Convert raw JSValueConst array to C++ Value array
            std::vector<Value> args;
            args.reserve(argc);
            for (int i = 0; i < argc; ++i) {
                args.emplace_back(ctx, argv[i], /*dup=*/true);
            }

            try {
                Value result = (*fn)(args);
                return result.release(); // Hand over reference ownership to JS
            } catch (const std::exception& e) {
                return JS_ThrowTypeError(ctx, "%s", e.what());
            } catch (...) {
                return JS_ThrowTypeError(ctx, "Unknown exception in native callback");
            }
        };

        // 4. Create an opaque payload object with our registered class
        JSValue opaque_val = JS_NewObjectClass(ctx, g_native_fn_class_id);
        JS_SetOpaque(opaque_val, fn_ptr);

        // 5. Wrap the trampoline and the opaque payload object into a JS Function
        JSValue func_val = JS_NewCFunctionData(ctx, trampoline, 0, 0, 1, &opaque_val);

        // Free our local reference to opaque_val (func_val holds a ref now)
        JS_FreeValue(ctx, opaque_val);

        return Value(ctx, func_val, /*dup=*/false);
    }

    bool Value::is_array() const noexcept {
        return ctx_ && JS_IsArray(val_) > 0;
    }

    bool Value::to_bool() const {
        return JS_ToBool(ctx_, val_) > 0;
    }

    int32_t Value::to_int32() const {
        int32_t res = 0;
        if (JS_ToInt32(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to int32");
        }
        return res;
    }

    double Value::to_double() const {
        double res = 0.0;
        if (JS_ToFloat64(ctx_, &res, val_) < 0) {
            throw std::runtime_error("Failed converting JSValue to double");
        }
        return res;
    }

    std::string Value::to_string() const {
        const char* str = JS_ToCString(ctx_, val_);
        if (!str) {
            throw std::runtime_error("Failed converting JSValue to CString");
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
        if (!ctx_) {
            throw std::runtime_error("Cannot get property: JSContext is null");
        }

        // Convert key to JSAtom to avoid allocation overhead on C-strings
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

    Value Value::get(uint32_t index) const {
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

    void Value::set(std::string_view key, const Value& val) {
        if (!ctx_) {
            throw std::runtime_error("Cannot set property: JSContext is null");
        }

        JSAtom atom = JS_NewAtomLen(ctx_, key.data(), key.size());

        // QuickJS's JS_SetProperty consumes a reference to the assigned value.
        // We clone `val` so our C++ `val` parameter retains its own ref-count safely.
        Value val_copy = val.clone();
        int res = JS_SetProperty(ctx_, val_, atom, val_copy.release());
        JS_FreeAtom(ctx_, atom);

        if (res < 0) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
            throw std::runtime_error("JS Set Property Error: " + exc_val.to_string());
        }
    }

    void Value::set(uint32_t index, const Value& val) {
        if (!ctx_) {
            throw std::runtime_error("Cannot set indexed property: JSContext is null");
        }

        // JS_SetPropertyUint32 also consumes a reference
        Value val_copy = val.clone();
        int res = JS_SetPropertyUint32(ctx_, val_, index, val_copy.release());

        if (res < 0) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
            throw std::runtime_error("JS Set Index Error: " + exc_val.to_string());
        }
    }

    Value Value::call(std::initializer_list<Value> args) const {
        Value undefined_this(ctx_, JS_UNDEFINED);
        return call_method(undefined_this, args);
    }

    Value Value::call_method(const Value& this_obj, std::initializer_list<Value> args) const {
        if (!ctx_) {
            throw std::runtime_error("Cannot call function: JSContext is null");
        }

        // Convert args to an array of raw JSValueConst handles
        std::vector<JSValueConst> raw_args;
        raw_args.reserve(args.size());
        for (const auto& arg : args) {
            raw_args.push_back(arg.raw());
        }

        // Invoke via QuickJS C API
        JSValue result_raw = JS_Call(
            ctx_,
            val_,
            this_obj.raw(),
            static_cast<int>(raw_args.size()),
            raw_args.data()
        );

        Value result(ctx_, result_raw, /*dup=*/false);

        // Check for runtime exceptions during function execution
        if (result.is_exception()) {
            Value exc_val(ctx_, JS_GetException(ctx_), /*dup=*/false);
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