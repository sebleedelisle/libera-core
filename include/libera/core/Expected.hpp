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
using expected = tl::expected<T, E>;

template <typename E>
using unexpected_t = tl::unexpected<E>;

template <typename E>
[[nodiscard]] constexpr unexpected_t<std::decay_t<E>> unexpected(E&& error) {
    return unexpected_t<std::decay_t<E>>(std::forward<E>(error));
}

} // namespace libera
