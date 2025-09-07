#pragma once
// Standalone Asio, no Boost.
#define ASIO_STANDALONE
#include <asio.hpp>
#include <system_error>

namespace netcfg {
    namespace asio = ::asio;
    using error_code = std::error_code;
    using tcp = asio::ip::tcp;
    using udp = asio::ip::udp;
}
