#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
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
        using MethodFunc = std::function<Value(T* instance, const std::vector<Value>& args)>;

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

        ClassBuilder& constructor(ConstructorFunc ctor) {
            ctor_ = std::move(ctor);
            return *this;
        }

        ClassBuilder& method(std::string_view name, MethodFunc func) {
            JSAtom atom = JS_NewAtomLen(ctx_, name.data(), name.size());

            auto trampoline = [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* data) -> JSValue {
                auto* fn_ptr = static_cast<MethodFunc*>(JS_GetOpaque(data[0], ClassId<T>::id));
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

            auto* heap_fn = new MethodFunc(std::move(func));
            JSValue opaque_val = JS_NewObjectClass(ctx_, ClassId<T>::id);
            JS_SetOpaque(opaque_val, heap_fn);

            JSValue fn_val = JS_NewCFunctionData(ctx_, trampoline, 0, 0, 1, &opaque_val);
            JS_FreeValue(ctx_, opaque_val);

            JS_SetProperty(ctx_, proto_.raw(), atom, fn_val);
            JS_FreeAtom(ctx_, atom);

            return *this;
        }

        Value build() {
            JS_SetClassProto(ctx_, ClassId<T>::id, proto_.clone().release());

            if (!ctor_) {
                return std::move(proto_);
            }

            // Allocate heap constructor callback
            auto* heap_ctor = new ConstructorFunc(std::move(ctor_));

            // Define trampoline receiving constructor pointer via JS_GetOpaque
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

            // Store payload in a native opaque object
            JSValue opaque_val = JS_NewObjectClass(ctx_, ClassId<T>::id);
            JS_SetOpaque(opaque_val, heap_ctor);

            // Create function using CFunctionData to properly retain data payload
            JSValue ctor_val = JS_NewCFunctionData(ctx_, ctor_trampoline, 0, 0, 1, &opaque_val);
            JS_FreeValue(ctx_, opaque_val);

            JS_SetConstructorBit(ctx_, ctor_val, 1);

            // Set standard JavaScript constructor and prototype relationship
            JS_SetConstructor(ctx_, ctor_val, proto_.raw());

            return {ctx_, ctor_val, false};
        }

    private:
        JSContext* ctx_;
        std::string class_name_;
        Value proto_;
        ConstructorFunc ctor_;
    };

} // namespace qjspp