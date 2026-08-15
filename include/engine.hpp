#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <memory>
#include <expected>
#include "value.hpp"

namespace qjspp {

    struct JsError {
        std::string name{"Error"};
        std::string message;
        std::string stack;
        std::string filename;
        int line_number{-1};

        [[nodiscard]] std::string to_string() const {
            std::string result = name + ": " + message;
            if (!stack.empty()) {
                result += "\nStack Trace:\n" + stack;
            }
            return result;
        }
    };

    class Engine {
    public:

        // Preset configurations
        [[nodiscard]] static Engine micro()   { return Engine(1 * 1024 * 1024,  256 * 1024); }  // 1MB RAM, 256KB stack
        [[nodiscard]] static Engine small()  { return Engine(8 * 1024 * 1024,  512 * 1024); }  // 8MB RAM, 512KB stack
        [[nodiscard]] static Engine medium() { return Engine(32 * 1024 * 1024, 1024 * 1024); } // 32MB RAM, 1MB stack
        [[nodiscard]] static Engine large()  { return Engine(128 * 1024 * 1024, 2048 * 1024); } // 128MB RAM, 2MB stack
        [[nodiscard]] static Engine unlimited()   { return {}; }

        // --- Lifecycle Management ---
        Engine();
        explicit Engine(size_t memory_limit, size_t stack_size = 0);
        ~Engine();

        // Prevent copying
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        // Allow move semantics
        Engine(Engine&& other) noexcept;
        Engine& operator=(Engine&& other) noexcept;

        // --- Core Execution Methods ---

        /// Evaluates a JavaScript snippet and returns a wrapped Value (or JsError on exception)
        [[nodiscard]] std::expected<Value, JsError> eval(
            std::string_view code,
            std::string_view filename = "<eval>",
            int eval_flags = JS_EVAL_TYPE_GLOBAL
        ) const;

        /// Runs a script in Global/Module mode, throws on exception, and returns the result Value
        [[nodiscard]] Value exec(
            std::string_view code,
            std::string_view filename = "<main>",
            int eval_flags = JS_EVAL_TYPE_GLOBAL
        ) const;

        /// Manually triggers QuickJS Garbage Collection
        void gc() const;

        // --- Value Factory Methods ---

        [[nodiscard]] Value make_undefined() const { return Value::make_undefined(ctx_); }
        [[nodiscard]] Value make_null() const { return Value::make_null(ctx_); }
        [[nodiscard]] Value make_bool(bool v) const { return Value::make_bool(ctx_, v); }
        [[nodiscard]] Value make_int(int32_t v) const { return Value::make_int(ctx_, v); }
        [[nodiscard]] Value make_long(int64_t v) const { return Value::make_long(ctx_, v); }
        [[nodiscard]] Value make_double(double v) const { return Value::make_double(ctx_, v); }
        [[nodiscard]] Value make_string(std::string_view str) const { return Value::make_string(ctx_, str); }
        [[nodiscard]] Value make_object() const { return Value::make_object(ctx_); }
        [[nodiscard]] Value make_array() const { return Value::make_array(ctx_); }
        [[nodiscard]] Value make_function(NativeFunction func) const { return Value::make_function(ctx_, std::move(func)); }

        [[nodiscard]] Value make_value(const int32_t v) const { return Value::make_int(ctx_, v); }
        [[nodiscard]] Value make_value(const double v) const { return Value::make_double(ctx_, v); }
        [[nodiscard]] Value make_value(const std::string_view v) const { return Value::make_string(ctx_, v); }

        // --- Direct Pointer Access ---
        [[nodiscard]] JSRuntime* runtime() const noexcept { return rt_; }
        [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

    private:
        JSRuntime* rt_{nullptr};
        JSContext* ctx_{nullptr};

        void check_exception(const Value& val) const;
        [[nodiscard]] std::string format_exception() const;
        [[nodiscard]] JsError get_and_clear_exception() const;
    };
}