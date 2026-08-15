#pragma once

#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <quickjs.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "class.hpp"
#include "module.hpp"
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
        [[nodiscard]] static Engine micro()   { return Engine(1 * 1024 * 1024,  256 * 1024); }
        [[nodiscard]] static Engine small()  { return Engine(8 * 1024 * 1024,  512 * 1024); }
        [[nodiscard]] static Engine medium() { return Engine(32 * 1024 * 1024, 1024 * 1024); }
        [[nodiscard]] static Engine large()  { return Engine(128 * 1024 * 1024, 2048 * 1024); }
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
        [[nodiscard]] std::expected<Value, JsError> eval(std::string_view code, std::string_view filename = "<eval>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        [[nodiscard]] std::expected<Value, JsError> eval_file(const std::filesystem::path& filepath, int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        void exec(std::string_view code, std::string_view filename = "<main>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        void exec_file(const std::filesystem::path& filepath, int eval_flags = JS_EVAL_TYPE_GLOBAL) const;

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

        template <typename T>
        ClassBuilder<T> make_class(std::string_view class_name) { return ClassBuilder<T>(context(), class_name); }
        ModuleBuilder new_module(std::string_view module_name) { return {context(), module_name}; }

        [[nodiscard]] Value global() const { return {ctx_, JS_GetGlobalObject(ctx_), false}; }

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

    // Helper function to read file contents into a string
    inline std::string read_file_content(const std::filesystem::path& filepath) {
        std::ifstream file(filepath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath.string());
        }

        std::string content;
        file.seekg(0, std::ios::end);
        content.resize(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(&content[0], content.size());

        return content;
    }

    // === INLINE IMPLEMENTATIONS ===

    inline Engine::Engine() : Engine(0, 0) {}

    inline Engine::Engine(const size_t memory_limit, const size_t stack_size) {
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
            throw std::runtime_error("Failed to create QuickJS Context");
        }
    }

    inline Engine::~Engine() {
        if (ctx_) {
            JS_FreeContext(ctx_);
        }
        if (rt_) {
            JS_FreeRuntime(rt_);
        }
    }

    inline Engine::Engine(Engine&& other) noexcept
        : rt_(std::exchange(other.rt_, nullptr)),
          ctx_(std::exchange(other.ctx_, nullptr)) {}

    inline Engine& Engine::operator=(Engine&& other) noexcept {
        if (this != &other) {
            if (ctx_) JS_FreeContext(ctx_);
            if (rt_) JS_FreeRuntime(rt_);

            rt_ = std::exchange(other.rt_, nullptr);
            ctx_ = std::exchange(other.ctx_, nullptr);
        }
        return *this;
    }

    inline void Engine::gc() const {
        if (rt_) {
            JS_RunGC(rt_);
        }
    }

    inline std::string Engine::format_exception() const {
        Value exception_val(ctx_, JS_GetException(ctx_), /*dup=*/false);

        std::string err_msg;
        try {
            err_msg = exception_val.to_string();
        } catch (...) {
            err_msg = "Unknown JavaScript Error";
        }

        if (exception_val.is_object()) {
            Value stack_val(ctx_, JS_GetPropertyStr(ctx_, exception_val.raw(), "stack"), /*dup=*/false);
            if (!stack_val.is_undefined()) {
                try {
                    std::string stack_str = stack_val.to_string();
                    if (!stack_str.empty()) {
                        err_msg += "\nStack Trace:\n" + stack_str;
                    }
                } catch (...) {}
            }
        }

        return err_msg;
    }

    inline void Engine::check_exception(const Value& val) const {
        if (val.is_exception()) {
            throw std::runtime_error(format_exception());
        }
    }

    inline JsError Engine::get_and_clear_exception() const {
        Value exception_val(ctx_, JS_GetException(ctx_), /*dup=*/false);

        JsError err;
        try {
            err.message = exception_val.to_string();
        } catch (...) {
            err.message = "Unknown JavaScript Error";
        }

        if (exception_val.is_object()) {
            Value stack_val(ctx_, JS_GetPropertyStr(ctx_, exception_val.raw(), "stack"), /*dup=*/false);
            if (!stack_val.is_undefined() && !stack_val.is_null()) {
                try {
                    err.stack = stack_val.to_string();
                } catch (...) {}
            }
        }

        return err;
    }

    inline std::expected<Value, JsError> Engine::eval(std::string_view code, std::string_view filename, int eval_flags) const {
        std::string code_str(code);
        std::string filename_str(filename);

        Value result(
            ctx_,
            JS_Eval(ctx_, code_str.c_str(), code_str.size(), filename_str.c_str(), eval_flags),
            false
        );

        if (result.is_exception()) {
            return std::unexpected(get_and_clear_exception());
        }

        return result;
    }

    inline void Engine::exec(std::string_view code, std::string_view filename, int eval_flags) const {
        std::string code_str(code);
        std::string filename_str(filename);

        Value result(
            ctx_,
            JS_Eval(ctx_, code_str.c_str(), code_str.size(), filename_str.c_str(), eval_flags),
            false
        );

        check_exception(result);
    }

    inline std::expected<Value, JsError> Engine::eval_file(const std::filesystem::path& filepath, int eval_flags) const {
        try {
            std::string code = read_file_content(filepath);
            return eval(code, filepath.string(), eval_flags);
        } catch (const std::exception& e) {
            JsError err;
            err.message = e.what();
            err.filename = filepath.string();
            return std::unexpected(err);
        }
    }

    inline void Engine::exec_file(const std::filesystem::path& filepath, int eval_flags) const {
        std::string code = read_file_content(filepath);
        return exec(code, filepath.string(), eval_flags);
    }

} // namespace qjspp