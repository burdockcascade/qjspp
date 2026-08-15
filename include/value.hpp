#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>

namespace qjspp {

    class Value {
    public:
        Value() noexcept;
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
        static Value make_int32(JSContext* ctx, int32_t v);
        static Value make_double(JSContext* ctx, double v);
        static Value make_string(JSContext* ctx, std::string_view str);
        static Value make_object(JSContext* ctx);
        static Value make_array(JSContext* ctx);

        // Keep small getters inline in the header for performance!
        [[nodiscard]] bool is_undefined() const noexcept { return JS_IsUndefined(val_); }
        [[nodiscard]] bool is_null() const noexcept { return JS_IsNull(val_); }
        [[nodiscard]] bool is_bool() const noexcept { return JS_IsBool(val_); }
        [[nodiscard]] bool is_number() const noexcept { return JS_IsNumber(val_); }
        [[nodiscard]] bool is_string() const noexcept { return JS_IsString(val_); }
        [[nodiscard]] bool is_object() const noexcept { return JS_IsObject(val_); }
        [[nodiscard]] bool is_exception() const noexcept { return JS_IsException(val_); }
        [[nodiscard]] bool is_array() const noexcept; // Moved to .cpp due to JSContext dependency

        // Conversions
        [[nodiscard]] bool to_bool() const;
        [[nodiscard]] int32_t to_int32() const;
        [[nodiscard]] double to_double() const;
        [[nodiscard]] std::string to_string() const;

        [[nodiscard]] Value call(std::initializer_list<Value> args) const;
        [[nodiscard]] Value call_method(const Value& this_obj, std::initializer_list<Value> args = {}) const;

        // --- Object & Array Property Accessors ---

        /// Checks if an object contains a property with the given name
        [[nodiscard]] bool has(std::string_view key) const;

        /// Gets a property value by key string
        [[nodiscard]] Value get(std::string_view key) const;

        /// Gets an array element or property by numeric index
        [[nodiscard]] Value get(uint32_t index) const;

        /// Sets a property value by key string
        void set(std::string_view key, const Value& val);

        /// Sets an array element or property by numeric index
        void set(uint32_t index, const Value& val);

        [[nodiscard]] JSValue raw() const noexcept { return val_; }
        [[nodiscard]] JSContext* context() const noexcept { return ctx_; }
        JSValue release() noexcept;

    private:
        JSContext* ctx_{nullptr};
        JSValue val_{JS_UNDEFINED};

        void free() noexcept;
    };

} // namespace qjspp