#pragma once

#include <asio.hpp>       // from libs/asio/include
#include <system_error>   // std::error_code

namespace libera::net {

/**
 * @brief Centralises networking aliases so higher-level code never includes Asio directly.
 *
 * Exposes:
 * - `libera::net::asio` as the standalone Asio namespace.
 * - `libera::net::tcp` and `libera::net::udp` as handy protocol aliases.
 */
namespace asio = ::asio;

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
} // namespace libera::net
