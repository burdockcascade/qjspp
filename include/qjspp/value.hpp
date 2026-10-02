#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <quickjs.h>
#include <string>
#include <string_view>
#include <vector>

#include "types.hpp"

namespace qjspp {

    template <typename T>
    struct ClassId;

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

        template <class T>
        static Value make_native_object(JSContext* ctx, std::unique_ptr<T> ptr);

        // Type Checks
        [[nodiscard]] bool is_undefined() const noexcept;
        [[nodiscard]] bool is_null() const noexcept;
        [[nodiscard]] bool is_bool() const noexcept;
        [[nodiscard]] bool is_number() const noexcept;
        [[nodiscard]] bool is_string() const noexcept;
        [[nodiscard]] bool is_object() const noexcept;
        [[nodiscard]] bool is_exception() const noexcept;
        [[nodiscard]] bool is_function() const noexcept;
        [[nodiscard]] bool is_array() const noexcept;

        // Conversions
        [[nodiscard]] bool to_bool() const;
        [[nodiscard]] int32_t to_int() const;
        [[nodiscard]] int64_t to_long() const;
        [[nodiscard]] double to_double() const;
        [[nodiscard]] float to_float() const;
        [[nodiscard]] std::string to_string() const;
        [[nodiscard]] std::vector<Value> to_vector() const;

        // Invocations
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
        [[nodiscard]] std::string fetch_and_clear_exception() const;
    };

}