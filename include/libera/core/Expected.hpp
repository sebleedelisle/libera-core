// Expected.hpp
// -----------------------------------------------------------------------------
// Central aliases for tl::expected / tl::unexpected so the rest of the codebase
// can name the success/error pair consistently. Default error type is
// std::error_code, but callers can override it when richer diagnostics are
// needed (e.g., schema decode errors).

#pragma once

#include <system_error>
#include <type_traits>
#include <utility>

#include <tl/expected.hpp>

namespace libera {

template <typename T, typename E = std::error_code>
using Expected = tl::expected<T, E>;

template <typename E>
using Unexpected = tl::unexpected<E>;

template <typename E>
[[nodiscard]] constexpr Unexpected<std::decay_t<E>> unexpected(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

} // namespace libera

