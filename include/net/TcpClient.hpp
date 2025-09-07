#pragma once
#include "net/NetConfig.hpp"
#include <chrono>
#include "net/Deadline.hpp"   // our timeout wrapper

namespace libera::net {
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace std::chrono;

/**
 * TcpClient
 *
 * - Provides a simple blocking-feel API on top of async operations.
 * - Supports:
 *     * connect with timeout
 *     * read_exact with timeout
 *     * write_all with timeout
 * - Also provides set_low_latency() (sets TCP_NODELAY + keepalive).
 */
class TcpClient {
public:
    explicit TcpClient(asio::io_context& io)
    : socket_(io)
    , strand_(asio::make_strand(io)) {}

    tcp::socket& socket() { return socket_; }

    /**
     * Connect to one of the provided endpoints, with a timeout.
     * Tries each endpoint in turn until one succeeds.
     */
    template<typename Endpoints>
    boost::system::error_code connect(Endpoints eps, milliseconds timeout) {
        boost::system::error_code last_ec = asio::error::host_not_found;
        for (auto& e : eps) {
            auto ec = connect_one(e.endpoint(), timeout);
            if (!ec) return ec; // success
            last_ec = ec;
            // Reset socket before trying next endpoint
            close();
            socket_ = tcp::socket(strand_.context());
        }
        return last_ec;
    }

    /**
     * Read exactly N bytes, or fail if timeout expires.
     */
    boost::system::error_code read_exact(void* buf, std::size_t n, milliseconds timeout) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto cb){
                asio::async_read(socket_, asio::buffer(buf, n), cb);
            },
            [&]{ socket_.cancel(); }
        );
    }

    /**
     * Write all N bytes, or fail if timeout expires.
     */
    boost::system::error_code write_all(const void* buf, std::size_t n, milliseconds timeout) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto cb){
                asio::async_write(socket_, asio::buffer(buf, n), cb);
            },
            [&]{ socket_.cancel(); }
        );
    }

    /**
     * Low-latency settings for streaming protocols (like EtherDream).
     */
    void set_low_latency() {
        boost::system::error_code ec;
        socket_.set_option(tcp::no_delay(true), ec);
        socket_.set_option(asio::socket_base::keep_alive(true), ec);
    }

    void close() {
        boost::system::error_code ignore;
        socket_.shutdown(tcp::socket::shutdown_both, ignore);
        socket_.close(ignore);
    }

private:
    boost::system::error_code connect_one(const tcp::endpoint& ep, milliseconds timeout) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto cb){ socket_.async_connect(ep, cb); },
            [&]{ socket_.cancel(); }
        );
    }

    tcp::socket socket_;
    asio::strand<asio::io_context::executor_type> strand_;
};

} // namespace libera::net
