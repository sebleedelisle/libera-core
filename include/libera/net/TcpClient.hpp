
// 3) libera::net::TcpClient
// A very thin wrapper around a TCP socket that adds per-operation timeouts
// and ensures serialized access.
//
// Key methods:
// - connect(endpoints, timeout) → connect with timeout (tries a sequence)
// - read_exact(buf, n, timeout) → read exactly N bytes with timeout
// - write_all(buf, n, timeout) → write N bytes with timeout
// - setLowLatency() → TCP_NODELAY + keepalive
// - close() → cancel + shutdown + close safely
//
// Asio details you might not use in openFrameworks:
// - We construct the socket with a `strand` executor. A strand guarantees that
//   handlers posted through it do not run concurrently. This effectively
//   serializes socket operations without manual mutexes.
// - Each I/O function uses `with_deadline(...)`: we start the async op and a
//   timer; cancel whichever loses. We then wait synchronously for completion
//   (your io_context must be running on some thread).

#pragma once
#include "libera/net/NetConfig.hpp"
#include "libera/net/Deadline.hpp"
#include "libera/net/TimeoutConfig.hpp"

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
    // We re-construct the socket with the strand each attempt to ensure a fresh
    // state (avoids leftover pending ops from prior attempts).
    template <typename Endpoints>
    error_code connect(const Endpoints& endpoints, milliseconds timeout = default_timeout()) {
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
    // SFINAE ensures we pick this when Results::value_type has endpoint().
    template <typename Results>
    error_code connect(Results results, milliseconds timeout = default_timeout(),
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

    error_code read_exact(void* buf, std::size_t n, milliseconds timeout = default_timeout()) {
        auto ex = socket_.get_executor();
        return with_deadline(ex, timeout,
            [&](auto completion){
                asio::async_read(socket_, asio::buffer(buf, n), completion);
            },
            [&]{ socket_.cancel(); }
        );
    }

    error_code write_all(const void* buf, std::size_t n, milliseconds timeout = default_timeout()) {
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
    // Useful during shutdown to nudge operations to complete now rather than
    // waiting for timeouts.
    void cancel() {
        error_code ec;
        socket_.cancel(ec);
    }

    void close() {
        if (!socket_.is_open()) return;
        error_code ec;
        // Proactively cancel any outstanding operations first (pattern: cancel → shutdown → close)
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

    error_code connect_one(const tcp::endpoint& ep, milliseconds timeout = default_timeout()) {
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
