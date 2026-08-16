#include <qjspp.hpp>

#include <fstream>
#include <stdexcept>

namespace qjspp {

    static std::string read_file_content(const std::filesystem::path& filepath) {
        std::ifstream file(filepath, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filepath.string());
        }

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        if (size < 0) {
            throw std::runtime_error("Failed to determine size of file: " + filepath.string());
        }
        file.seekg(0, std::ios::beg);

        std::string content;
        content.resize_and_overwrite(size, [&file](char* buf, size_t n) {
            file.read(buf, n);
            return file.gcount();
        });

        return content;
    }

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
            rt_ = nullptr;
            throw std::runtime_error("Failed to create QuickJS Context");
        }
    }

    Engine::~Engine() {
        if (ctx_) {
            JS_FreeContext(ctx_);
            ctx_ = nullptr;
        }
        if (rt_) {
            JS_FreeRuntime(rt_);
            rt_ = nullptr;
        }
    }

    Engine::Engine(Engine&& other) noexcept
        : rt_(std::exchange(other.rt_, nullptr)),
          ctx_(std::exchange(other.ctx_, nullptr)) {}

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
        return get_and_clear_exception().to_string();
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
            if (exception_val.has("stack")) {
                try {
                    Value stack_val = exception_val.get("stack");
                    if (!stack_val.is_undefined() && !stack_val.is_null()) {
                        err.stack = stack_val.to_string();
                    }
                } catch (...) {}
            }

            if (exception_val.has("fileName")) {
                try {
                    Value file_val = exception_val.get("fileName");
                    if (file_val.is_string()) {
                        err.filename = file_val.to_string();
                    }
                } catch (...) {}
            }

            if (exception_val.has("lineNumber")) {
                try {
                    Value line_val = exception_val.get("lineNumber");
                    if (line_val.is_number()) {
                        err.line_number = line_val.to_int();
                    }
                } catch (...) {}
            }
        }

        return err;
    }

    std::expected<Value, JsError> Engine::eval(std::string_view code, const char* filename, int eval_flags) const {
        Value result(
            ctx_,
            JS_Eval(ctx_, code.data(), code.size(), filename, eval_flags),
            false
        );

        if (result.is_exception()) {
            return std::unexpected(get_and_clear_exception());
        }

        return result;
    }

    void Engine::exec(std::string_view code, const char* filename, int eval_flags) const {
        Value result(
            ctx_,
            JS_Eval(ctx_, code.data(), code.size(), filename, eval_flags),
            false
        );

        check_exception(result);
    }

    std::expected<Value, JsError> Engine::eval_file(const std::filesystem::path& filepath, int eval_flags) const {
        try {
            std::string code = read_file_content(filepath);
            return eval(code, filepath.string().c_str(), eval_flags);
        } catch (const std::exception& e) {
            JsError err;
            err.message = e.what();
            err.filename = filepath.string();
            return std::unexpected(err);
        }
    }

    void Engine::exec_file(const std::filesystem::path& filepath, int eval_flags) const {
        std::string code = read_file_content(filepath);
        exec(code, filepath.string().c_str(), eval_flags);
    }

} // namespace qjspp