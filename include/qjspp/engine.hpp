#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <quickjs.h>
#include <span>
#include <string_view>
#include <utility>

#include "class_builder.hpp"
#include "module_builder.hpp"
#include "types.hpp"
#include "value.hpp"

namespace qjspp {

    class Engine {
    public:
        // Presets
        [[nodiscard]] static Engine micro()   { return Engine(1 * 1024 * 1024,   256 * 1024); }
        [[nodiscard]] static Engine small()  { return Engine(8 * 1024 * 1024,   512 * 1024); }
        [[nodiscard]] static Engine medium() { return Engine(32 * 1024 * 1024,  1024 * 1024); }
        [[nodiscard]] static Engine large()  { return Engine(128 * 1024 * 1024, 2048 * 1024); }

        Engine();
        explicit Engine(size_t memory_limit, size_t stack_size = 0);
        ~Engine();

        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        Engine(Engine&& other) noexcept;
        Engine& operator=(Engine&& other) noexcept;

        [[nodiscard]] QJSVersion version() {
            return {QJS_VERSION_MAJOR, QJS_VERSION_MINOR, QJS_VERSION_PATCH};
        }

        // Exec / Eval
        [[nodiscard]] std::expected<Value, JsError> eval(std::string_view code, const char* filename = "<eval>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        [[nodiscard]] std::expected<Value, JsError> eval_file(const std::filesystem::path& filepath, int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        void exec(std::string_view code, const char* filename = "<main>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        void exec_file(const std::filesystem::path& filepath, int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        void exec_bytecode(std::span<const uint8_t> bytes) const;

        void gc() const;

        // Value Factories
        [[nodiscard]] Value make_undefined() const { return Value::make_undefined(ctx_); }
        [[nodiscard]] Value make_null() const { return Value::make_null(ctx_); }
        [[nodiscard]] Value make_bool(const bool v) const { return Value::make_bool(ctx_, v); }
        [[nodiscard]] Value make_int(const int32_t v) const { return Value::make_int(ctx_, v); }
        [[nodiscard]] Value make_long(const int64_t v) const { return Value::make_long(ctx_, v); }
        [[nodiscard]] Value make_double(const double v) const { return Value::make_double(ctx_, v); }
        [[nodiscard]] Value make_string(const std::string_view str) const { return Value::make_string(ctx_, str); }
        [[nodiscard]] Value make_object() const { return Value::make_object(ctx_); }
        [[nodiscard]] Value make_array() const { return Value::make_array(ctx_); }
        [[nodiscard]] Value make_function(NativeFunction func) const { return Value::make_function(ctx_, std::move(func)); }

        template <typename T>
        [[nodiscard]] Value make_native_object(std::unique_ptr<T> ptr) const { return Value::make_native_object(ctx_, std::move(ptr)); }

        [[nodiscard]] Value make_value(const int32_t v) const { return Value::make_int(ctx_, v); }
        [[nodiscard]] Value make_value(const double v) const { return Value::make_double(ctx_, v); }
        [[nodiscard]] Value make_value(const std::string_view v) const { return Value::make_string(ctx_, v); }

        template <typename T>
        ClassBuilder<T> make_class(std::string_view class_name) { return ClassBuilder<T>(context(), class_name); }
        [[nodiscard]] ModuleBuilder new_module(std::string_view module_name) const { return {context(), module_name}; }

        [[nodiscard]] Value global() const { return {ctx_, JS_GetGlobalObject(ctx_), false}; }

        void set_global(const std::string_view name, const Value& val) const { global().set(name, val); }
        void set_global(const std::string_view name, const bool val) const { set_global(name, make_bool(val)); }
        void set_global(const std::string_view name, const int32_t val) const { set_global(name, make_int(val)); }
        void set_global(const std::string_view name, const int64_t val) const { set_global(name, make_long(val)); }
        void set_global(const std::string_view name, const double val) const { set_global(name, make_double(val)); }
        void set_global(const std::string_view name, const std::string_view val) const { set_global(name, make_string(val)); }

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