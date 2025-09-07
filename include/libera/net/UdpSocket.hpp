#pragma once
#include "libera/net/NetConfig.hpp"
#include <chrono>
#include "libera/net/Deadline.hpp"

namespace libera::net {

/**
 * UdpSocket
 *
 * - Simplifies opening/binding/sending/receiving with UDP.
 * - Provides timeout-capable send_to / recv_from.
 * - Useful for discovery protocols (broadcast/multicast).
 */
class UdpSocket {
public:
    explicit UdpSocket(asio::io_context& io) : sock_(io) {}

    libera::net::error_code open_v4() {
        libera::net::error_code ec;
        sock_.open(udp::v4(), ec);
        return ec;
    }

    libera::net::error_code bind_any(uint16_t port) {
        libera::net::error_code ec;
        sock_.bind(udp::endpoint(udp::v4(), port), ec);
        return ec;
    }

    libera::net::error_code enable_broadcast(bool on=true) {
        libera::net::error_code ec;
        sock_.set_option(asio::socket_base::broadcast(on), ec);
        return ec;
    }

    // Send a datagram, fail if not sent within timeout.
    libera::net::error_code send_to(const void* data, std::size_t n,
                                      const udp::endpoint& ep, milliseconds timeout) {
        auto ex = sock_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto cb){ sock_.async_send_to(asio::buffer(data, n), ep, 0, cb); },
            [&]{ sock_.cancel(); });
    }

    // Receive one datagram, with timeout. Returns ec + fills out_ep + out_n.
    libera::net::error_code recv_from(void* data, std::size_t max,
                                        udp::endpoint& out_ep, std::size_t& out_n,
                                        milliseconds timeout) {
        auto ex = sock_.get_executor();
        out_n = 0;
        return with_deadline(ex, timeout,
            [&](auto cb){
                sock_.async_receive_from(asio::buffer(data, max), out_ep, 0,
                    [&, cb](const libera::net::error_code& ec, std::size_t n){
                        out_n = n; cb(ec);
                    });
            },
            [&]{ sock_.cancel(); });
    }

    udp::socket& raw() { return sock_; }
    void close() { libera::net::error_code ignore; sock_.close(ignore); }

private:
    udp::socket sock_;
};

} // namespace libera::net
