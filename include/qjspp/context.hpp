#pragma once

#include <cstddef>
#include <quickjs.h>
#include <span>
#include "value.hpp"

namespace qjspp {

    class CallContext {
    public:
        CallContext(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) noexcept
            : ctx_(ctx),
              this_val_(this_val),
              args_(argv, static_cast<size_t>(argc > 0 ? argc : 0)) {}

        CallContext(JSContext* ctx, JSValueConst this_val, std::span<const JSValueConst> args) noexcept
            : ctx_(ctx), this_val_(this_val), args_(args) {}

        [[nodiscard]] size_t size() const noexcept { return args_.size(); }
        [[nodiscard]] bool empty() const noexcept { return args_.empty(); }
        [[nodiscard]] JSContext* context() const noexcept { return ctx_; }
        [[nodiscard]] std::span<const JSValueConst> raw_span() const noexcept { return args_; }

        [[nodiscard]] Value get_this() const;
        [[nodiscard]] Value operator[](size_t index) const;

        [[nodiscard]] JSValueConst raw(size_t index) const noexcept {
            return index < args_.size() ? args_[index] : JS_UNDEFINED;
        }

    private:
        JSContext* ctx_{nullptr};
        JSValueConst this_val_{JS_UNDEFINED};
        std::span<const JSValueConst> args_{};
    };

}