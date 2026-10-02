#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <quickjs.h>
#include <string>

namespace qjspp {

    class Value;
    class CallContext;

    using NativeFunction = std::function<Value(const CallContext& args)>;

    inline std::atomic<JSClassID> g_native_fn_class_id{0};
    inline std::atomic<JSClassID> g_fn_meta_class_id{0};

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

    struct QJSVersion {
        int major{0};
        int minor{0};
        int patch{0};

        [[nodiscard]] std::string to_string() const {
            return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        }
    };

    struct TypeErasedFn {
        void* ptr{nullptr};
        void (*deleter)(void*){nullptr};

        ~TypeErasedFn() {
            if (ptr && deleter) {
                deleter(ptr);
            }
        }
    };

}