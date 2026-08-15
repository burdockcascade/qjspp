#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <memory>
#include <expected>

namespace qjspp {

    struct JsError {
        std::string name{"Error"};
        std::string message;
        std::string stack;
        std::string filename;
        int line_number{-1};

        /// Helper to format the full error nicely for printing/logging
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
        // --- Lifecycle Management ---
        Engine();
        explicit Engine(size_t memory_limit, size_t stack_size = 0);
        ~Engine();

        // Prevent copying to avoid double-freeing QuickJS native pointers
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        // Allow move semantics
        Engine(Engine&& other) noexcept;
        Engine& operator=(Engine&& other) noexcept;

        // --- Core Execution Methods ---

        /// Evaluates a JavaScript snippet and returns string result (or throws on error)
        [[nodiscard]] std::expected<std::string, JsError> eval(std::string_view code, std::string_view filename = "<eval>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;

        /// Runs a script in Global/Module mode
        void exec(std::string_view code, std::string_view filename = "<main>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;

        /// Manually triggers QuickJS Garbage Collection
        void gc() const;

        // --- Direct Pointer Access ---
        [[nodiscard]] JSRuntime* runtime() const noexcept { return rt_; }
        [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

    private:
        JSRuntime* rt_{nullptr};
        JSContext* ctx_{nullptr};

        void check_exception(JSValueConst result_val) const;
        [[nodiscard]] std::string format_exception() const;
        [[nodiscard]] JsError get_and_clear_exception() const;
    };
}