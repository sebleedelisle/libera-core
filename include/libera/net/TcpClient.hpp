#pragma once
#include "libera/net/NetConfig.hpp"
#include "libera/net/Deadline.hpp"
#include "libera/net/TimeoutConfig.hpp"
#include "libera/net/NetService.hpp"
#include "libera/log/Log.hpp"

#include <chrono>
#include <memory>

namespace libera::net {
using std::chrono::milliseconds;

/**
 * @brief Thin wrapper around `tcp::socket` that adds deadlines and low-latency options.
 *
 * Highlights:
 * - `connect(...)` retries endpoints and respects a per-attempt timeout.
 * - `read_exact(...)` and `write_all(...)` block the caller while enforcing deadlines.
 * - `setLowLatency()` enables TCP_NODELAY and keepalive to reduce jitter.
 * - All socket work is serialized by a strand executor.
 *
 * The caller must keep the owning `asio::io_context` running while using the API.
 */
class TcpClient {
public:
    TcpClient()
    : io_(shared_io_context())
    , socket_(*io_)
    , strand_(asio::make_strand(*io_))
    {}

    // Access to the socket (non-const)
    tcp::socket& socket() { return socket_; }

    // Overload 1: connect from a range/container of *endpoints* (e.g., std::array<tcp::endpoint, N>)
    // Delegates to the single-endpoint overload so each attempt resets the socket
    // before calling connect_one().
    std::error_code connect(const tcp::endpoint& endpoint, long long timeoutMillis = default_timeout()) {
        close();
        socket_ = tcp::socket(strand_);
        return connect_one(endpoint, timeoutMillis);
    }

    template <typename Endpoints>
    std::error_code connect(const Endpoints& endpoints, long long timeoutMillis = default_timeout()) {
        std::error_code last = asio::error::host_not_found;

        for (const auto& e : endpoints) {
            auto ec = connect(endpoint_of(e), timeoutMillis);
            if (!ec) return ec;   // success
            last = ec;            // remember last error and try next
        }
        return last;
    }

    // Overload 2: connect from resolver results (entries have .endpoint())
    // SFINAE ensures we pick this when Results::value_type has endpoint().
    template <typename Results>
    std::error_code connect(Results results, long long timeoutMillis = default_timeout(),
                       // SFINAE: prefer this when value_type has endpoint()
                       decltype(std::declval<typename Results::value_type>().endpoint(), 0) = 0) {
        std::error_code last = asio::error::host_not_found;

        for (auto& e : results) {
            auto ec = connect(e.endpoint(), timeoutMillis);
            if (!ec) return ec;
            last = ec;
        }
        return last;
    }

    std::error_code read_exact(void* buf, std::size_t n, long long timeoutMillis = default_timeout(),
                               std::size_t* bytesTransferredOut = nullptr) {
        auto ex = socket_.get_executor();
        const auto timeout = clamp_to_milliseconds(timeoutMillis);
        std::size_t bytesTransferred = 0;
        auto ec = with_deadline(ex, timeout,
            [&](auto completion){
                asio::async_read(socket_, asio::buffer(buf, n),
                    [&, completion](const std::error_code& op_ec, std::size_t transferred){
                        bytesTransferred = transferred;
                        completion(op_ec);
                    });
            },
            [&]{ socket_.cancel(); }
        );
        if (bytesTransferredOut) {
            *bytesTransferredOut = bytesTransferred;
        }
        return ec;
    }

    std::error_code write_all(const void* buf, std::size_t n, long long timeoutMillis = default_timeout()) {
        auto ex = socket_.get_executor();
        const auto timeout = clamp_to_milliseconds(timeoutMillis);
        auto ec = with_deadline(ex, timeout,
            [&](auto completion){
                asio::async_write(socket_, asio::buffer(buf, n),
                    [completion](const std::error_code& op_ec, std::size_t){
                        completion(op_ec);
                    });
            },
            [&]{ socket_.cancel(); }
        );
        return ec;
    }

    void setLowLatency() {
        std::error_code ec;
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
        std::error_code ec;
        socket_.cancel(ec);
    }

        void close() {
            logInfo("[TcpClient] close()\n");
        if (!socket_.is_open()) return;
        std::error_code ec;
        // Proactively cancel outstanding operations first (pattern: cancel -> shutdown -> close).
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

    std::error_code connect_one(const tcp::endpoint& ep, long long timeoutMillis = default_timeout()) {
        auto ex = socket_.get_executor();
        const auto timeout = clamp_to_milliseconds(timeoutMillis);
        return with_deadline(ex, timeout,
            [&](auto completion){ socket_.async_connect(ep, completion); },
            [&]{ socket_.cancel(); }
        );
    }

    static milliseconds clamp_to_milliseconds(long long timeoutMillis) {
        if (timeoutMillis <= 0) {
            return milliseconds{0};
        }
        return milliseconds{timeoutMillis};
    }

    std::shared_ptr<asio::io_context> io_;
    tcp::socket socket_;
    asio::strand<asio::io_context::executor_type> strand_;
};

} // namespace libera::net
