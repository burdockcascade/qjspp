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
        Value exception_val(ctx_, JS_GetException(ctx_), /*dup=*/false);

        std::string err_msg;
        try {
            err_msg = exception_val.to_string();
        } catch (...) {
            err_msg = "Unknown JavaScript Error";
        }

        // Directly query "stack" if it's an object
        if (exception_val.is_object()) {
            Value stack_val(ctx_, JS_GetPropertyStr(ctx_, exception_val.raw(), "stack"), /*dup=*/false);
            if (!stack_val.is_undefined()) {
                try {
                    std::string stack_str = stack_val.to_string();
                    if (!stack_str.empty()) {
                        err_msg += "\nStack Trace:\n" + stack_str;
                    }
                } catch (...) {}
            }
        }

        return err_msg;
    }

    void Engine::check_exception(const Value& val) const {
        if (val.is_exception()) {
            throw std::runtime_error(format_exception());
        }
    }

    JsError Engine::get_and_clear_exception() const {
        Value exception_val(ctx_, JS_GetException(ctx_), /*dup=*/false);

        JsError err;
        try {
            err.message = exception_val.to_string();
        } catch (...) {
            err.message = "Unknown JavaScript Error";
        }

        if (exception_val.is_object()) {
            Value stack_val(ctx_, JS_GetPropertyStr(ctx_, exception_val.raw(), "stack"), /*dup=*/false);
            if (!stack_val.is_undefined() && !stack_val.is_null()) {
                try {
                    err.stack = stack_val.to_string();
                } catch (...) {}
            }
        }

        return err;
    }

    std::expected<Value, JsError> Engine::eval(std::string_view code, std::string_view filename, int eval_flags) const {
        std::string code_str(code);
        std::string filename_str(filename);

        Value result(
            ctx_,
            JS_Eval(ctx_, code_str.c_str(), code_str.size(), filename_str.c_str(), eval_flags),
            /*dup=*/false
        );

        if (result.is_exception()) {
            return std::unexpected(get_and_clear_exception());
        }

        return result;
    }

    Value Engine::exec(std::string_view code, std::string_view filename, int eval_flags) const {
        std::string code_str(code);
        std::string filename_str(filename);

        Value result(
            ctx_,
            JS_Eval(ctx_, code_str.c_str(), code_str.size(), filename_str.c_str(), eval_flags),
            /*dup=*/false
        );

        check_exception(result);
        return result;
    }

}