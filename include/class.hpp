#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <quickjs.h>
#include "value.hpp"

namespace qjspp {

    // Unique JSClassID allocator per C++ type T
    template <typename T>
    struct ClassId {
        static JSClassID id;
    };

    template <typename T>
    JSClassID ClassId<T>::id = 0;

    // Helper functions for native pointer conversions
    template <typename T>
    Value make_native_object(JSContext* ctx, std::unique_ptr<T> ptr) {
        if (!ctx || !ptr) return {};
        JSClassID cid = ClassId<T>::id;
        if (cid == 0) return {};

        JSValue obj = JS_NewObjectClass(ctx, cid);
        if (JS_IsException(obj)) return {ctx, obj, false};

        // Relinquish unique_ptr ownership only after object creation succeeds
        JS_SetOpaque(obj, ptr.release());
        return {ctx, obj, false};
    }

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
        using ConstructorFunc = std::function<std::unique_ptr<T>(const std::vector<Value>& args)>;
        using InstanceMethodFunc = std::function<Value(T* instance, const std::vector<Value>& args)>;
        using StaticMethodFunc = std::function<Value(const std::vector<Value>& args)>;

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

                    std::vector<Value> args;
                    args.reserve(argc);
                    for (int i = 0; i < argc; ++i) {
                        args.emplace_back(ctx, argv[i], true);
                    }

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

                    std::vector<Value> args;
                    args.reserve(argc);
                    for (int i = 0; i < argc; ++i) {
                        args.emplace_back(ctx, argv[i], true);
                    }

                    std::unique_ptr<T> instance = (*ctor_ptr)(args);
                    return make_native_object(ctx, std::move(instance)).release();
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

                        std::vector<Value> args;
                        args.reserve(argc);
                        for (int i = 0; i < argc; ++i) {
                            args.emplace_back(ctx, argv[i], true);
                        }

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

} // namespace qjspp