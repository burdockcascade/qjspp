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

    Value Value::make_undefined(JSContext* ctx) { return {ctx, JS_UNDEFINED}; }
    Value Value::make_null(JSContext* ctx) { return {ctx, JS_NULL}; }
    Value Value::make_bool(JSContext* ctx, bool v) { return {ctx, JS_NewBool(ctx, v)}; }
    Value Value::make_int32(JSContext* ctx, int32_t v) { return {ctx, JS_NewInt32(ctx, v)}; }
    Value Value::make_double(JSContext* ctx, double v) { return {ctx, JS_NewFloat64(ctx, v)}; }
    Value Value::make_string(JSContext* ctx, std::string_view str) {
        return {ctx, JS_NewStringLen(ctx, str.data(), str.size())};
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