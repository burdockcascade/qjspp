#pragma once

#include <atomic>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <ios>
#include <memory>
#include <quickjs.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qjspp {

    // =========================================================================
    // Declarations & ArgList
    // =========================================================================

    class Value;

    class ArgList {
    public:
        // Update constructor to take this_val
        ArgList(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) noexcept
            : ctx_(ctx), this_val_(this_val), argc_(argc), argv_(argv) {}

        [[nodiscard]] size_t size() const noexcept { return static_cast<size_t>(argc_); }
        [[nodiscard]] bool empty() const noexcept { return argc_ == 0; }
        [[nodiscard]] JSContext* context() const noexcept { return ctx_; }

        // Retrieve 'this' as an owning Value wrapper
        [[nodiscard]] Value get_this() const;

        // Creates an owning Value on-demand only when accessed
        [[nodiscard]] Value operator[](size_t index) const;

        // Provides raw access for zero-overhead type checking
        [[nodiscard]] JSValueConst raw(size_t index) const noexcept {
            if (index >= static_cast<size_t>(argc_)) return JS_UNDEFINED;
            return argv_[index];
        }

    private:
        JSContext* ctx_;
        JSValueConst this_val_{JS_UNDEFINED};
        int argc_;
        JSValueConst* argv_;
    };

    using NativeFunction = std::function<Value(const ArgList& args)>;

    // Global state for native functions using atomic initialization
    inline std::atomic<JSClassID> g_native_fn_class_id{0};

    // =========================================================================
    // Value Interface
    // =========================================================================

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

        template<class T>
        static Value make_native_object(JSContext *ctx, std::unique_ptr<T> ptr);

        // Checks
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

    // =========================================================================
    // Native Class Utilities & ClassBuilder
    // =========================================================================

    // Unique JSClassID allocator per C++ type T
    template <typename T>
    struct ClassId {
        static JSClassID id;
    };

    template <typename T>
    JSClassID ClassId<T>::id = 0;

    template <typename T>
    T* get_native_opaque(const Value& val) {
        if (!val.context() || !val.is_object()) return nullptr;
        JSClassID cid = ClassId<T>::id;
        if (cid == 0) return nullptr;
        return static_cast<T*>(JS_GetOpaque(val.raw(), cid));
    }

    // Type-erased container for function opaque metadata objects to guarantee correct deletion
    struct TypeErasedFn {
        void* ptr{nullptr};
        void (*deleter)(void*){nullptr};

        ~TypeErasedFn() {
            if (ptr && deleter) {
                deleter(ptr);
            }
        }
    };

    // Dedicated ClassID and finalizer for Function Wrapper metadata objects
    inline std::atomic<JSClassID> g_fn_meta_class_id{0};

    template <typename Fn>
    JSValue create_function_opaque(JSContext* ctx, Fn* fn_ptr) {
        if (!fn_ptr) return JS_UNDEFINED;

        JSRuntime* rt = JS_GetRuntime(ctx);
        JSClassID class_id = g_fn_meta_class_id.load(std::memory_order_relaxed);

        // 1. Allocate the global Class ID (if not already done)
        if (class_id == 0) {
            JS_NewClassID(rt, &class_id);
            g_fn_meta_class_id.store(class_id, std::memory_order_relaxed);
        }

        // 2. Register the class *for this specific runtime* (if not already done)
        if (!JS_IsRegisteredClass(rt, class_id)) {
            JSClassDef class_def{};
            class_def.class_name = "CppFunctionMetadata";
            class_def.finalizer = [](JSRuntime*, JSValue val) {
                JSClassID current_id = g_fn_meta_class_id.load(std::memory_order_relaxed);
                auto* wrapper = static_cast<TypeErasedFn*>(JS_GetOpaque(val, current_id));
                delete wrapper; // Invokes ~TypeErasedFn() which calls the stored type-safe deleter
            };
            JS_NewClass(rt, class_id, &class_def);
        }

        auto* wrapper = new TypeErasedFn{
            .ptr = fn_ptr,
            .deleter = [](void* p) { delete static_cast<Fn*>(p); }
        };

        JSValue opaque_val = JS_NewObjectClass(ctx, class_id);
        if (JS_IsException(opaque_val)) {
            delete wrapper;
            return JS_UNDEFINED;
        }

        JS_SetOpaque(opaque_val, wrapper);
        return opaque_val;
    }

    template <typename Fn>
    Fn* get_function_opaque(JSValueConst val) {
        auto* wrapper = static_cast<TypeErasedFn*>(JS_GetOpaque(val, g_fn_meta_class_id));
        return wrapper ? static_cast<Fn*>(wrapper->ptr) : nullptr;
    }

    // Builder class to expose C++ classes to QuickJS
    template <typename T>
    class ClassBuilder {
    public:
        using ConstructorFunc = std::function<std::unique_ptr<T>(const ArgList& args)>;
        using InstanceMethodFunc = std::function<Value(T* instance, const ArgList& args)>;
        using StaticMethodFunc = std::function<Value(const ArgList& args)>;

        using PropertyGetterFunc = std::function<Value(JSContext* ctx, T* instance)>;
        using PropertySetterFunc = std::function<void(T* instance, const Value& val)>;

        ClassBuilder(JSContext* ctx, std::string_view class_name) : ctx_(ctx), class_name_(class_name) {
            JSRuntime* rt = JS_GetRuntime(ctx_);

            // 1. Allocate the global Class ID (if not already done)
            if (ClassId<T>::id == 0) {
                JS_NewClassID(rt, &ClassId<T>::id);
            }

            // 2. Register the class *for this specific runtime* (if not already done)
            if (!JS_IsRegisteredClass(rt, ClassId<T>::id)) {
                JSClassDef class_def{};
                class_def.class_name = class_name_.c_str();
                class_def.finalizer = [](JSRuntime*, JSValue val) {
                    auto* ptr = static_cast<T*>(JS_GetOpaque(val, ClassId<T>::id));
                    delete ptr;
                };

                JS_NewClass(rt, ClassId<T>::id, &class_def);
            }

            proto_ = Value::make_object(ctx_);
        }

        void constructor(ConstructorFunc ctor) {
            ctor_ = std::move(ctor);
        }

        void instance_method(std::string_view name, InstanceMethodFunc func) {
            JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());

            auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                try {
                    auto* fn_ptr = get_function_opaque<InstanceMethodFunc>(data[0]);
                    auto* inst = static_cast<T*>(JS_GetOpaque(this_val, ClassId<T>::id));
                    if (!inst || !fn_ptr || !*fn_ptr) {
                        return JS_ThrowTypeError(ctx, "Invalid Native Object Instance or Method");
                    }

                    ArgList args(ctx, this_val, argc, argv);
                    Value res = (*fn_ptr)(inst, args);
                    return res.release();
                } catch (const std::exception& e) {
                    return JS_ThrowTypeError(ctx, "%s", e.what());
                } catch (...) {
                    return JS_ThrowTypeError(ctx, "Unknown exception in native instance method");
                }
            };

            auto* heap_fn = new InstanceMethodFunc(std::move(func));
            JSValue opaque_val = create_function_opaque(ctx_, heap_fn);

            JSValue fn_val = JS_NewCFunctionData(ctx_, trampoline, 0, 0, 1, &opaque_val);
            JS_FreeValue(ctx_, opaque_val);

            JS_SetProperty(ctx_, proto_.raw(), atom, fn_val);
            JS_FreeAtom(ctx_, atom);
        }

        void static_method(std::string_view name, StaticMethodFunc func) {
            static_methods_.emplace_back(std::string(name), std::move(func));
        }

        void property(std::string_view name, PropertyGetterFunc getter, PropertySetterFunc setter = nullptr) {
            JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());
            JSValue getter_val = JS_UNDEFINED;
            JSValue setter_val = JS_UNDEFINED;

            if (getter) {
                auto getter_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                    try {
                        auto* fn_ptr = get_function_opaque<PropertyGetterFunc>(data[0]);
                        auto* inst = static_cast<T*>(JS_GetOpaque(this_val, ClassId<T>::id));
                        if (!inst || !fn_ptr || !*fn_ptr) return JS_ThrowTypeError(ctx, "Invalid Native Object Instance or Getter");

                        return (*fn_ptr)(ctx, inst).release();
                    } catch (const std::exception& e) {
                        return JS_ThrowTypeError(ctx, "%s", e.what());
                    } catch (...) {
                        return JS_ThrowTypeError(ctx, "Unknown exception in native getter");
                    }
                };

                auto* heap_getter = new PropertyGetterFunc(std::move(getter));
                JSValue opaque_val = create_function_opaque(ctx_, heap_getter);

                getter_val = JS_NewCFunctionData(ctx_, getter_trampoline, 0, 0, 1, &opaque_val);
                JS_FreeValue(ctx_, opaque_val);
            }

            if (setter) {
                auto setter_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                    try {
                        auto* fn_ptr = get_function_opaque<PropertySetterFunc>(data[0]);
                        auto* inst = static_cast<T*>(JS_GetOpaque(this_val, ClassId<T>::id));
                        if (!inst || !fn_ptr || !*fn_ptr) return JS_ThrowTypeError(ctx, "Invalid Native Object Instance or Setter");

                        (*fn_ptr)(inst, Value(ctx, argv[0], true));
                        return JS_UNDEFINED;
                    } catch (const std::exception& e) {
                        return JS_ThrowTypeError(ctx, "%s", e.what());
                    } catch (...) {
                        return JS_ThrowTypeError(ctx, "Unknown exception in native setter");
                    }
                };

                auto* heap_setter = new PropertySetterFunc(std::move(setter));
                JSValue opaque_val = create_function_opaque(ctx_, heap_setter);

                setter_val = JS_NewCFunctionData(ctx_, setter_trampoline, 1, 0, 1, &opaque_val);
                JS_FreeValue(ctx_, opaque_val);
            }

            JS_DefinePropertyGetSet(
                ctx_,
                proto_.raw(),
                atom,
                getter_val,
                setter_val,
                JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE
            );

            JS_FreeAtom(ctx_, atom);
        }

        Value build() {
            JS_SetClassProto(ctx_, ClassId<T>::id, proto_.clone().release());

            if (!ctor_ && static_methods_.empty()) {
                return std::move(proto_);
            }

            auto ctor_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                try {
                    auto* ctor_ptr = get_function_opaque<ConstructorFunc>(data[0]);
                    if (!ctor_ptr || !*ctor_ptr) {
                        return JS_ThrowTypeError(ctx, "Constructor call failed");
                    }

                    ArgList args(ctx, this_val, argc, argv);

                    std::unique_ptr<T> instance = (*ctor_ptr)(args);
                    return Value::make_native_object(ctx, std::move(instance)).release();
                } catch (const std::exception& e) {
                    return JS_ThrowTypeError(ctx, "%s", e.what());
                } catch (...) {
                    return JS_ThrowTypeError(ctx, "Unknown exception in native constructor");
                }
            };

            JSValue ctor_val = JS_UNDEFINED;
            if (ctor_) {
                auto* heap_ctor = new ConstructorFunc(std::move(ctor_));
                JSValue opaque_val = create_function_opaque(ctx_, heap_ctor);

                ctor_val = JS_NewCFunctionData(ctx_, ctor_trampoline, 0, 0, 1, &opaque_val);
                JS_FreeValue(ctx_, opaque_val);

                JS_SetConstructorBit(ctx_, ctor_val, true);
                JS_SetConstructor(ctx_, ctor_val, proto_.raw());
            } else {
                ctor_val = JS_NewObject(ctx_);
            }

            for (auto& [name, func] : static_methods_) {
                JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());

                auto static_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                    try {
                        auto* fn_ptr = get_function_opaque<StaticMethodFunc>(data[0]);
                        if (!fn_ptr || !*fn_ptr) return JS_ThrowTypeError(ctx, "Invalid static method call");

                        ArgList args(ctx, this_val, argc, argv);

                        return (*fn_ptr)(args).release();
                    } catch (const std::exception& e) {
                        return JS_ThrowTypeError(ctx, "%s", e.what());
                    } catch (...) {
                        return JS_ThrowTypeError(ctx, "Unknown exception in static method");
                    }
                };

                auto* heap_fn = new StaticMethodFunc(std::move(func));
                JSValue static_opaque = create_function_opaque(ctx_, heap_fn);

                JSValue fn_val = JS_NewCFunctionData(ctx_, static_trampoline, 0, 0, 1, &static_opaque);
                JS_FreeValue(ctx_, static_opaque);

                JS_SetProperty(ctx_, ctor_val, atom, fn_val);
                JS_FreeAtom(ctx_, atom);
            }

            return {ctx_, ctor_val, false};
        }

    private:
        JSContext* ctx_;
        std::string class_name_;
        Value proto_;
        ConstructorFunc ctor_;
        std::vector<std::pair<std::string, StaticMethodFunc>> static_methods_;
    };

    // =========================================================================
    // ModuleBuilder
    // =========================================================================

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, std::string_view name)
            : ctx_(ctx), name_(name) {}

        // Add a Value export to the module
        void export_value(std::string_view export_name, Value val) {
            exports_.emplace_back(std::string(export_name), std::move(val));
        }

        // Add a function export using NativeFunction callback
        void export_function(std::string_view export_name, NativeFunction func) {
            export_value(export_name, Value::make_function(ctx_, std::move(func)));
        }

        void export_class(std::string_view export_name, Value val) {
            export_value(export_name, std::move(val));
        }

        // Builds and registers the ES module with QuickJS
        void finalize();

    private:
        JSContext* ctx_{nullptr};
        std::string name_;
        std::vector<std::pair<std::string, Value>> exports_;
    };

    // =========================================================================
    // Engine & JsError
    // =========================================================================

    struct JsError {
        std::string name{"Error"};
        std::string message;
        std::string stack;
        std::string filename;
        int line_number{-1};

        [[nodiscard]] std::string to_string() const {
            std::string result = message;
            if (!filename.empty() && line_number >= 0) {
                result += " (" + filename + ":" + std::to_string(line_number) + ")";
            }
            if (!stack.empty()) {
                result += "\nStack Trace:\n" + stack;
            }
            return result;
        }
    };

    class Engine {
    public:
        // Preset configurations
        [[nodiscard]] static Engine micro()   { return Engine(1 * 1024 * 1024,   256 * 1024); }  //  1 MB heap,  256 KB buffer
        [[nodiscard]] static Engine small()  { return Engine(8 * 1024 * 1024,   512 * 1024); }  //  8 MB heap,  512 KB buffer
        [[nodiscard]] static Engine medium() { return Engine(32 * 1024 * 1024,  1024 * 1024); } // 32 MB heap, 1024 KB buffer
        [[nodiscard]] static Engine large()  { return Engine(128 * 1024 * 1024, 2048 * 1024); } // 128 MB heap, 2048 KB buffer

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
        [[nodiscard]] std::expected<Value, JsError> eval(std::string_view code, const char* filename = "<eval>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        [[nodiscard]] std::expected<Value, JsError> eval_file(const std::filesystem::path& filepath, int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
        void exec(std::string_view code, const char* filename = "<main>", int eval_flags = JS_EVAL_TYPE_GLOBAL) const;
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

        template <typename T>
        [[nodiscard]] Value make_native_object(std::unique_ptr<T> ptr) const { return Value::make_native_object(ctx_, std::move(ptr)); }

        [[nodiscard]] Value make_value(const int32_t v) const { return Value::make_int(ctx_, v); }
        [[nodiscard]] Value make_value(const double v) const { return Value::make_double(ctx_, v); }
        [[nodiscard]] Value make_value(const std::string_view v) const { return Value::make_string(ctx_, v); }

        template <typename T>
        ClassBuilder<T> make_class(std::string_view class_name) { return ClassBuilder<T>(context(), class_name); }
        [[nodiscard]] ModuleBuilder new_module(std::string_view module_name) const { return {context(), module_name}; }

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

    template<class T>
    Value Value::make_native_object(JSContext* ctx, std::unique_ptr<T> ptr) {
        if (!ptr || !ctx) {
            return make_null(ctx);
        }

        JSRuntime* rt = JS_GetRuntime(ctx);
        JSClassID class_id = ClassId<T>::id;

        // Ensure class is registered
        if (class_id == 0) {
            JS_NewClassID(rt, &ClassId<T>::id);
            class_id = ClassId<T>::id;
        }

        if (!JS_IsRegisteredClass(rt, class_id)) {
            JSClassDef class_def{};
            class_def.class_name = typeid(T).name();
            class_def.finalizer = [](JSRuntime*, JSValue val) {
                auto* instance = static_cast<T*>(JS_GetOpaque(val, ClassId<T>::id));
                delete instance;
            };
            JS_NewClass(rt, class_id, &class_def);
        }

        JSValue obj = JS_NewObjectClass(ctx, class_id);
        if (JS_IsException(obj)) {
            return make_undefined(ctx);
        }

        // Transfer ownership of raw pointer to QuickJS object
        T* raw_ptr = ptr.release();
        JS_SetOpaque(obj, raw_ptr);

        // Attach prototype if it exists
        JSValue proto = JS_GetClassProto(ctx, class_id);
        if (!JS_IsUndefined(proto)) {
            JS_SetPrototype(ctx, obj, proto);
            JS_FreeValue(ctx, proto);
        }

        return Value(ctx, obj, false);
    }

    // =========================================================================
    // Templated Functions
    // =========================================================================

    template <typename Class, typename T>
    void add_property_getset(auto& obj, const char* name, T Class::* member) {
        obj.property(
            name,
            // Getter
            [member](JSContext* ctx, Class* self) {
                using DecayedT = std::decay_t<T>;

                if constexpr (std::is_same_v<DecayedT, bool>) {
                    return Value::make_bool(ctx, self->*member);
                }
                else if constexpr (std::is_same_v<DecayedT, std::string>) {
                    return Value::make_string(ctx, (self->*member).c_str());
                }
                else if constexpr (std::is_same_v<DecayedT, const char*>) {
                    return Value::make_string(ctx, self->*member);
                }
                else if constexpr (std::is_enum_v<DecayedT>) {
                    using Underlying = std::underlying_type_t<DecayedT>;
                    return Value::make_int(ctx, static_cast<int32_t>(static_cast<Underlying>(self->*member)));
                }
                else if constexpr (std::is_integral_v<DecayedT>) {
                    if constexpr (std::is_unsigned_v<DecayedT> && sizeof(DecayedT) >= 4) {
                        return Value::make_double(ctx, static_cast<double>(self->*member));
                    } else {
                        return Value::make_int(ctx, static_cast<int32_t>(self->*member));
                    }
                }
                else if constexpr (std::is_floating_point_v<DecayedT>) {
                    return Value::make_double(ctx, static_cast<double>(self->*member));
                }
                else {
                    return Value::make_native_object(ctx, std::make_unique<DecayedT>(self->*member));
                }
            },
            // Setter
            [member](Class* self, const Value& val) {
                using DecayedT = std::decay_t<T>;

                if constexpr (std::is_same_v<DecayedT, bool>) {
                    self->*member = val.to_bool();
                }
                else if constexpr (std::is_same_v<DecayedT, std::string>) {
                    self->*member = val.to_string();
                }
                else if constexpr (std::is_enum_v<DecayedT>) {
                    using Underlying = std::underlying_type_t<DecayedT>;
                    self->*member = static_cast<DecayedT>(static_cast<Underlying>(val.to_int()));
                }
                else if constexpr (std::is_integral_v<DecayedT>) {
                    if constexpr (std::is_unsigned_v<DecayedT> && sizeof(DecayedT) >= 4) {
                        self->*member = static_cast<DecayedT>(val.to_double());
                    } else {
                        self->*member = static_cast<DecayedT>(val.to_int());
                    }
                }
                else if constexpr (std::is_floating_point_v<DecayedT>) {
                    self->*member = static_cast<DecayedT>(val.to_double());
                }
                else if constexpr (!std::is_same_v<DecayedT, const char*>) {
                    auto* ptr = qjspp::get_native_opaque<DecayedT>(val);
                    if (ptr) {
                        self->*member = *ptr;
                    }
                }
            }
        );
    }

} // namespace qjspp