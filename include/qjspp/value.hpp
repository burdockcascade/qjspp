#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
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

        // Optional Conversions (no throw, cleans exception context)
        [[nodiscard]] std::optional<bool> as_bool() const noexcept;
        [[nodiscard]] std::optional<int32_t> as_int() const noexcept;
        [[nodiscard]] std::optional<int64_t> as_long() const noexcept;
        [[nodiscard]] std::optional<double> as_double() const noexcept;
        [[nodiscard]] std::optional<float> as_float() const noexcept;
        [[nodiscard]] std::optional<std::string> as_string() const noexcept;
        [[nodiscard]] std::optional<std::vector<Value>> as_vector() const noexcept;

        // Default-fallback Conversions
        [[nodiscard]] bool to_bool(bool default_val = false) const noexcept {
            return as_bool().value_or(default_val);
        }
        [[nodiscard]] int32_t to_int(int32_t default_val = 0) const noexcept {
            return as_int().value_or(default_val);
        }
        [[nodiscard]] int64_t to_long(int64_t default_val = 0) const noexcept {
            return as_long().value_or(default_val);
        }
        [[nodiscard]] double to_double(double default_val = 0.0) const noexcept {
            return as_double().value_or(default_val);
        }
        [[nodiscard]] float to_float(float default_val = 0.0f) const noexcept {
            return as_float().value_or(default_val);
        }
        [[nodiscard]] std::string to_string(std::string_view default_val = "") const {
            return as_string().value_or(std::string(default_val));
        }
        [[nodiscard]] std::vector<Value> to_vector(std::vector<Value> default_val = {}) const {
            return as_vector().value_or(std::move(default_val));
        }

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
        void clear_exception() const noexcept;
        [[nodiscard]] std::string fetch_and_clear_exception() const;
    };

} // namespace qjspp