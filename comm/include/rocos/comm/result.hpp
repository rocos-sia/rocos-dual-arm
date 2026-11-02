#pragma once
#include <optional>
#include <string>
#include <utility>

namespace rocos::comm {
enum class Error {
    ok = 0, unavailable = 1, timeout = 2, busy = 3, conflict = 4,
    invalid_argument = 5, transport = 6, serialization = 7,
    rejected = 8, handler_error = 9, closed = 10, no_data = 11, incompatible_type = 12
};
struct Status {
    Error code{Error::ok};
    std::string detail;
    explicit operator bool() const { return code == Error::ok; }
};
template<class T> struct Result {
    Status status;
    std::optional<T> value;
    explicit operator bool() const { return bool(status) && value.has_value(); }
    static Result success(T value) { return {{}, std::move(value)}; }
    static Result failure(Error code, std::string detail) {
        return {{code, std::move(detail)}, std::nullopt};
    }
};
const char* error_name(Error code);
}
