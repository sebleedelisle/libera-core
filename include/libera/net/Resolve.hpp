#pragma once
#include "libera/net/NetConfig.hpp"
#include <string>

namespace libera::net {
using tcp = asio::ip::tcp;

/**
 * resolve
 *
 * - Performs DNS resolution (host + service string -> list of endpoints).
 * - Returns netconfig::error_code if it fails.
 */
inline libera::net::error_code resolve(
    asio::io_context& io,
    const std::string& host,
    const std::string& service,
    tcp::resolver::results_type& out)
{
    libera::net::error_code ec;
    tcp::resolver r(io);
    out = r.resolve(host, service, ec);
    return ec;
}

} // namespace libera::net
