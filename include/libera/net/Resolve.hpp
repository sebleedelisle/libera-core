#pragma once
#include "libera/net/NetConfig.hpp"
#include <string>

namespace libera::net {
using tcp = asio::ip::tcp;

/**
 * resolve
 *
 * Simple synchronous DNS lookup helper. Given `host` and `service` (e.g.
 * "example.com", "http" or "7765") it returns a list of endpoints. Use this
 * if you want to connect by name rather than raw IPs. It is used with the
 * TCP client connect overload that accepts resolver results.
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
