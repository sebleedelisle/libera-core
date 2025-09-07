#pragma once
#include "net/NetConfig.hpp"
#include <string>

namespace libera::net {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

/**
 * resolve
 *
 * - Performs DNS resolution (host + service string -> list of endpoints).
 * - Returns boost::system::error_code if it fails.
 */
inline boost::system::error_code resolve(
    asio::io_context& io,
    const std::string& host,
    const std::string& service,
    tcp::resolver::results_type& out)
{
    boost::system::error_code ec;
    tcp::resolver r(io);
    out = r.resolve(host, service, ec);
    return ec;
}

} // namespace libera::net
