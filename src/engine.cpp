#include "engine.hpp"
#include <utility>

namespace qjspp {

    Engine::Engine() : Engine(0, 0) {}

    Engine::Engine(const size_t memory_limit, const size_t stack_size) {
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

    Engine::~Engine() {
        if (ctx_) {
            JS_FreeContext(ctx_);
        }
        if (rt_) {
            JS_FreeRuntime(rt_);
        }
    }

    // Move Constructor
    Engine::Engine(Engine&& other) noexcept
        : rt_(std::exchange(other.rt_, nullptr)),
          ctx_(std::exchange(other.ctx_, nullptr)) {}

    // Move Assignment
    Engine& Engine::operator=(Engine&& other) noexcept {
        if (this != &other) {
            if (ctx_) JS_FreeContext(ctx_);
            if (rt_) JS_FreeRuntime(rt_);

            rt_ = std::exchange(other.rt_, nullptr);
            ctx_ = std::exchange(other.ctx_, nullptr);
        }
        return *this;
    }

    void Engine::gc() const {
        if (rt_) {
            JS_RunGC(rt_);
        }
    }

    std::string Engine::format_exception() const {
        JSValue exception_val = JS_GetException(ctx_);

        const char* str = JS_ToCString(ctx_, exception_val);
        std::string err_msg = str ? str : "Unknown JavaScript Error";
        if (str) {
            JS_FreeCString(ctx_, str);
        }

        // Directly query "stack" if it's an object
        if (JS_IsObject(exception_val)) {
            JSValue stack_val = JS_GetPropertyStr(ctx_, exception_val, "stack");
            if (!JS_IsUndefined(stack_val)) {
                const char* stack_str = JS_ToCString(ctx_, stack_val);
                if (stack_str && stack_str[0] != '\0') {
                    err_msg += "\nStack Trace:\n";
                    err_msg += stack_str;
                }
                if (stack_str) {
                    JS_FreeCString(ctx_, stack_str);
                }
            }
            JS_FreeValue(ctx_, stack_val);
        }

        JS_FreeValue(ctx_, exception_val);
        return err_msg;
    }

    void Engine::check_exception(JSValueConst val) const {
        if (JS_IsException(val)) {
            throw std::runtime_error(format_exception());
        }
    }

    JsError Engine::get_and_clear_exception() const {
        JSValue exception_val = JS_GetException(ctx_);

        JsError err;
        const char* str = JS_ToCString(ctx_, exception_val);
        err.message = str ? str : "Unknown JavaScript Error";
        if (str) JS_FreeCString(ctx_, str);

        if (JS_IsObject(exception_val)) {
            JSValue stack_val = JS_GetPropertyStr(ctx_, exception_val, "stack");
            if (!JS_IsUndefined(stack_val) && !JS_IsNull(stack_val)) {
                const char* stack_str = JS_ToCString(ctx_, stack_val);
                if (stack_str) {
                    err.stack = stack_str;
                    JS_FreeCString(ctx_, stack_str);
                }
            }
            JS_FreeValue(ctx_, stack_val);
        }

        JS_FreeValue(ctx_, exception_val);
        return err;
    }

    std::expected<std::string, JsError> Engine::eval(std::string_view code, std::string_view filename, int eval_flags) const {
        std::string code_str(code);
        std::string filename_str(filename);

        const JSValue result = JS_Eval(
            ctx_,
            code_str.c_str(),
            code_str.size(),
            filename_str.c_str(),
            eval_flags
        );

        if (JS_IsException(result)) {
            return std::unexpected(get_and_clear_exception());
        }

        const char* str = JS_ToCString(ctx_, result);
        std::string return_value = str ? str : "";
        if (str) JS_FreeCString(ctx_, str);
        JS_FreeValue(ctx_, result);

        return return_value;
    }

    void Engine::exec(std::string_view code, std::string_view filename, int eval_flags) const {
        const JSValue result = JS_Eval(
            ctx_,
            code.data(),
            code.size(),
            filename.data(),
            eval_flags
        );

        check_exception(result);
        JS_FreeValue(ctx_, result);
    }

}