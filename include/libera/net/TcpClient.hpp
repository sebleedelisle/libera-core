
// 3) libera::net::TcpClient
// What: a very thin wrapper around a TCP socket that adds timeouts.
// Key methods:
// connect(endpoints, timeout) → connect with timeout
// read_exact(buf, n, timeout) → read exactly N bytes with timeout
// write_all(buf, n, timeout) → write N bytes with timeout
// set_low_latency() → TCP_NODELAY + keepalive
// close() → shutdown + close safely
// Inside:
// owns tcp::socket socket_
// owns an asio::strand executor to serialize socket ops
// each operation calls with_deadline(...) under the hood
// Think of it like: the same socket you’d write with raw Asio, but with “per-call timeout” sugar.

#pragma once
#include "libera/net/NetConfig.hpp"
#include "libera/net/Deadline.hpp"

#include <chrono>

namespace libera::net {
using namespace std::chrono;

/**
 * TcpClient
 *  - connect() with timeout
 *  - write_all() with timeout
 *  - read_exact() with timeout
 *  - set_low_latency() (TCP_NODELAY + keepalive)
 *
 * Assumes the asio::io_context is running elsewhere.
 */
class TcpClient {
public:
    explicit TcpClient(asio::io_context& io)
    : socket_(io)
    , strand_(asio::make_strand(io))
    {}

    // Access to the socket (non-const)
    tcp::socket& socket() { return socket_; }

    // Overload 1: connect from a range/container of *endpoints* (e.g., std::array<tcp::endpoint, N>)
    template <typename Endpoints>
    error_code connect(const Endpoints& endpoints, milliseconds timeout) {
        error_code last = asio::error::host_not_found;

        for (const auto& e : endpoints) {
            // Fresh socket for each attempt: construct with the strand executor
            close();
            socket_ = tcp::socket(strand_);   // NOTE: pass the executor, not the context

            auto ec = connect_one(endpoint_of(e), timeout);
            if (!ec) return ec;   // success
            last = ec;            // remember last error and try next
        }
        return last;
    }

    // Overload 2: connect from resolver results (entries have .endpoint())
    template <typename Results>
    error_code connect(Results results, milliseconds timeout,
                       // SFINAE: prefer this when value_type has endpoint()
                       decltype(std::declval<typename Results::value_type>().endpoint(), 0) = 0) {
        error_code last = asio::error::host_not_found;

        for (auto& e : results) {
            close();
            socket_ = tcp::socket(strand_);

            auto ec = connect_one(e.endpoint(), timeout);
            if (!ec) return ec;
            last = ec;
        }
        return last;
    }

    error_code read_exact(void* buf, std::size_t n, milliseconds timeout) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto completion){
                asio::async_read(socket_, asio::buffer(buf, n), completion);
            },
            [&]{ socket_.cancel(); }
        );
    }

    error_code write_all(const void* buf, std::size_t n, milliseconds timeout) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto completion){
                asio::async_write(socket_, asio::buffer(buf, n), completion);
            },
            [&]{ socket_.cancel(); }
        );
    }

    void setLowLatency() {
        error_code ec;
        socket_.set_option(tcp::no_delay(true), ec);
        socket_.set_option(asio::socket_base::keep_alive(true), ec);
    }

    bool is_open() const { return socket_.is_open(); }
    // auto& socket() { return socket_; }
    // const auto& socket() const { return socket_; }

    // Best-effort cancellation of pending ops on the socket.
    void cancel() {
        error_code ec;
        socket_.cancel(ec);
    }

    void close() {
        if (!socket_.is_open()) return;
        error_code ec;
        // Proactively cancel any outstanding operations first
        socket_.cancel(ec);
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    // Helper to accept either a tcp::endpoint or a resolver result entry.
    static tcp::endpoint endpoint_of(const tcp::endpoint& ep) { return ep; }

    template <typename Entry>
    static auto endpoint_of(const Entry& e) -> decltype(e.endpoint()) {
        return e.endpoint();
    }

    error_code connect_one(const tcp::endpoint& ep, milliseconds timeout) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto completion){ socket_.async_connect(ep, completion); },
            [&]{ socket_.cancel(); }
        );
    }

    tcp::socket socket_;
    asio::strand<asio::io_context::executor_type> strand_;
};

} // namespace libera::net
