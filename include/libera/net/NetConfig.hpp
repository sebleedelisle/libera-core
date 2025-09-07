
// libera::net::NetConfig
// What: a tiny header that gives you one namespace for networking stuff.
// Why: so higher-level code never touches boost::asio vs asio directly.
// You get:
// libera::net::asio → the Asio API (standalone in your setup)
// libera::net::error_code → std::error_code
// libera::net::tcp / udp → handy aliases
// Think of this as a translation layer so the rest of your code is consistent.

#pragma once

#include <asio.hpp>       // from libs/asio/include
#include <system_error>   // std::error_code




namespace libera::net {

    // Expose standalone Asio under libera::net::asio
    namespace asio = ::asio;

    // Use std::error_code (standalone Asio integrates with std)
    using error_code = std::error_code;

    // Shorthand type aliases for convenience
    using tcp = asio::ip::tcp;
    using udp = asio::ip::udp;

} // namespace libera::net