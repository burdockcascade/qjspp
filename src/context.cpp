#include "qjspp/context.hpp"
#include "qjspp/value.hpp"

namespace qjspp {

    Value CallContext::get_this() const {
        return {ctx_, this_val_, /*dup=*/true};
    }

    Value CallContext::operator[](size_t index) const {
        if (index >= args_.size()) {
            return Value::make_undefined(ctx_);
        }
        return {ctx_, args_[index], /*dup=*/true};
    }

} // namespace qjspp