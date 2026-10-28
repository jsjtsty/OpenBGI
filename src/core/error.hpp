#pragma once
#include <expected>
#include <string>
#include <utility>

namespace obgi {

enum class Errc { Io, Format, Range, Unsupported };

struct Error {
    Errc code;
    std::string message;
};

template <class T> using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(Errc code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

} // namespace obgi
