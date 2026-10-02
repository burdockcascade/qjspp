#pragma once

#include <quickjs.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "types.hpp"
#include "value.hpp"

namespace qjspp {

    class ModuleBuilder {
    public:
        ModuleBuilder(JSContext* ctx, std::string_view name)
            : ctx_(ctx), name_(name) {}

        void export_value(std::string_view export_name, Value val) {
            exports_.emplace_back(std::string(export_name), std::move(val));
        }

        void export_function(std::string_view export_name, NativeFunction func) {
            export_value(export_name, Value::make_function(ctx_, std::move(func)));
        }

        void export_class(std::string_view export_name, Value val) {
            export_value(export_name, std::move(val));
        }

        void finalize();

    private:
        JSContext* ctx_{nullptr};
        std::string name_;
        std::vector<std::pair<std::string, Value>> exports_;
    };

}