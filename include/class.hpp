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

    // Builder class to expose C++ classes to QuickJS
    template <typename T>
    class ClassBuilder {
    public:
        using ConstructorFunc = std::function<std::unique_ptr<T>(const std::vector<Value>& args)>;
        using InstanceMethodFunc = std::function<Value(T* instance, const std::vector<Value>& args)>;
        using StaticMethodFunc = std::function<Value(const std::vector<Value>& args)>;

        // Signatures for property getters and setters
        using PropertyGetterFunc = std::function<Value(JSContext* ctx, T* instance)>;
        using PropertySetterFunc = std::function<void(T* instance, const Value& val)>;

        ClassBuilder(JSContext* ctx, std::string_view class_name)
            : ctx_(ctx), class_name_(class_name) {
            JSRuntime* rt = JS_GetRuntime(ctx_);

            if (ClassId<T>::id == 0) {
                JS_NewClassID(rt, &ClassId<T>::id);
            }

            JSClassDef class_def{};
            class_def.class_name = class_name_.c_str();
            class_def.finalizer = [](JSRuntime*, JSValue val) {
                auto* ptr = static_cast<T*>(JS_GetOpaque(val, ClassId<T>::id));
                delete ptr;
            };

            JS_NewClass(rt, ClassId<T>::id, &class_def);
            proto_ = Value::make_object(ctx_);
        }

        void constructor(ConstructorFunc ctor) {
            ctor_ = std::move(ctor);
        }

        void instance_method(std::string_view name, InstanceMethodFunc func) {
            JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());

            auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                auto* fn_ptr = static_cast<InstanceMethodFunc*>(JS_GetOpaque(data[0], ClassId<T>::id));
                auto* inst = static_cast<T*>(JS_GetOpaque(this_val, ClassId<T>::id));
                if (!inst) {
                    return JS_ThrowTypeError(ctx, "Invalid Native Object Instance");
                }

                std::vector<Value> args;
                args.reserve(argc);
                for (int i = 0; i < argc; ++i) {
                    args.emplace_back(ctx, argv[i], true);
                }

                try {
                    Value res = (*fn_ptr)(inst, args);
                    return res.release();
                } catch (const std::exception& e) {
                    return JS_ThrowTypeError(ctx, "%s", e.what());
                }
            };

            auto* heap_fn = new InstanceMethodFunc(std::move(func));
            JSValue opaque_val = JS_NewObjectClass(ctx_, ClassId<T>::id);
            JS_SetOpaque(opaque_val, heap_fn);

            JSValue fn_val = JS_NewCFunctionData(ctx_, trampoline, 0, 0, 1, &opaque_val);
            JS_FreeValue(ctx_, opaque_val);

            JS_SetProperty(ctx_, proto_.raw(), atom, fn_val);
            JS_FreeAtom(ctx_, atom);

        }

        void static_method(std::string_view name, StaticMethodFunc func) {
            static_methods_.emplace_back(std::string(name), std::move(func));
        }

        // Registers a getter (and optionally a setter) property
        void property(std::string_view name, PropertyGetterFunc getter, PropertySetterFunc setter = nullptr) {
            JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());
            JSValue getter_val = JS_UNDEFINED;
            JSValue setter_val = JS_UNDEFINED;

            if (getter) {
                auto getter_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                    auto* fn_ptr = static_cast<PropertyGetterFunc*>(JS_GetOpaque(data[0], ClassId<T>::id));
                    auto* inst = static_cast<T*>(JS_GetOpaque(this_val, ClassId<T>::id));
                    if (!inst) return JS_ThrowTypeError(ctx, "Invalid Native Object Instance");

                    try {
                        return (*fn_ptr)(ctx, inst).release();
                    } catch (const std::exception& e) {
                        return JS_ThrowTypeError(ctx, "%s", e.what());
                    }
                };

                auto* heap_getter = new PropertyGetterFunc(std::move(getter));
                JSValue opaque_val = JS_NewObjectClass(ctx_, ClassId<T>::id);
                JS_SetOpaque(opaque_val, heap_getter);

                getter_val = JS_NewCFunctionData(ctx_, getter_trampoline, 0, 0, 1, &opaque_val);
                JS_FreeValue(ctx_, opaque_val);
            }

            if (setter) {
                auto setter_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                    auto* fn_ptr = static_cast<PropertySetterFunc*>(JS_GetOpaque(data[0], ClassId<T>::id));
                    auto* inst = static_cast<T*>(JS_GetOpaque(this_val, ClassId<T>::id));
                    if (!inst) return JS_ThrowTypeError(ctx, "Invalid Native Object Instance");

                    try {
                        (*fn_ptr)(inst, Value(ctx, argv[0], true));
                        return JS_UNDEFINED;
                    } catch (const std::exception& e) {
                        return JS_ThrowTypeError(ctx, "%s", e.what());
                    }
                };

                auto* heap_setter = new PropertySetterFunc(std::move(setter));
                JSValue opaque_val = JS_NewObjectClass(ctx_, ClassId<T>::id);
                JS_SetOpaque(opaque_val, heap_setter);

                setter_val = JS_NewCFunctionData(ctx_, setter_trampoline, 1, 0, 1, &opaque_val);
                JS_FreeValue(ctx_, opaque_val);
            }

            // Bind the getter and setter configuration natively
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

            auto* heap_ctor = ctor_ ? new ConstructorFunc(std::move(ctor_)) : nullptr;

            auto ctor_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                auto* ctor_ptr = static_cast<ConstructorFunc*>(JS_GetOpaque(data[0], ClassId<T>::id));
                if (!ctor_ptr || !*ctor_ptr) {
                    return JS_ThrowTypeError(ctx, "Constructor call failed");
                }

                std::vector<Value> args;
                args.reserve(argc);
                for (int i = 0; i < argc; ++i) {
                    args.emplace_back(ctx, argv[i], true);
                }

                try {
                    std::unique_ptr<T> instance = (*ctor_ptr)(args);
                    return make_native_object(ctx, std::move(instance)).release();
                } catch (const std::exception& e) {
                    return JS_ThrowTypeError(ctx, "%s", e.what());
                }
            };

            JSValue opaque_val = JS_NewObjectClass(ctx_, ClassId<T>::id);
            JS_SetOpaque(opaque_val, heap_ctor);

            JSValue ctor_val = JS_NewCFunctionData(ctx_, ctor_trampoline, 0, 0, 1, &opaque_val);
            JS_FreeValue(ctx_, opaque_val);

            JS_SetConstructorBit(ctx_, ctor_val, true);
            JS_SetConstructor(ctx_, ctor_val, proto_.raw());

            for (auto& [name, func] : static_methods_) {
                JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());

                auto static_trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                    auto* fn_ptr = static_cast<StaticMethodFunc*>(JS_GetOpaque(data[0], ClassId<T>::id));
                    if (!fn_ptr || !*fn_ptr) return JS_ThrowTypeError(ctx, "Invalid static method call");

                    std::vector<Value> args;
                    args.reserve(argc);
                    for (int i = 0; i < argc; ++i) {
                        args.emplace_back(ctx, argv[i], true);
                    }

                    try {
                        return (*fn_ptr)(args).release();
                    } catch (const std::exception& e) {
                        return JS_ThrowTypeError(ctx, "%s", e.what());
                    }
                };

                auto* heap_fn = new StaticMethodFunc(std::move(func));
                JSValue static_opaque = JS_NewObjectClass(ctx_, ClassId<T>::id);
                JS_SetOpaque(static_opaque, heap_fn);

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