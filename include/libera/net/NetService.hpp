#pragma once
#include "libera/net/NetConfig.hpp"
#include <thread>
#include <iostream>
#include <memory>

namespace libera::net {

/**
 * NetService
 *
 * What it is:
 * - A tiny RAII wrapper around `asio::io_context` that starts a dedicated
 *   background thread to drive all asynchronous I/O and timers.
 *
 * Why this pattern:
 * - In plain openFrameworks you often do blocking I/O on the main thread.
 *   Here we lean into Asio's async model: we run a single I/O thread and
 *   post all socket/timer work to it. This keeps the app responsive and
 *   avoids tricky multi-thread access to sockets.
 *
 * Key pieces:
 * - `asio::io_context` (shared_ptr-owned) is the event loop for all async operations.
 * - `executor_work_guard` prevents the loop from exiting when idle.
 * - A background `std::thread` calls `io_.run()` until destruction.
 *
 * Lifetime and shutdown:
 * - Destroy network-using objects (controllers, sockets) before NetService so
 *   their async handlers are cancelled/finished while the io_context still runs.
 * - In the destructor we reset the work guard, call `stop()`, and join the
 *   thread—this is the orderly shutdown sequence recommended by Asio.
 *
 * Convenience helpers:
 * - Most code uses `libera::net::shared_io_context()` or `io_context()` to grab
 *   the process-wide I/O loop owned by a static NetService instance. Tests can
 *   still instantiate their own NetService for isolation.
 */
class NetService {
public:
    NetService();
    ~NetService();

    NetService(const NetService&) = delete;
    NetService& operator=(const NetService&) = delete;
    NetService(NetService&&) = delete;
    NetService& operator=(NetService&&) = delete;

    std::shared_ptr<asio::io_context> io() { return io_; }

private:
    std::shared_ptr<asio::io_context> io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::thread t_;
};

NetService& ensureNetService();
std::shared_ptr<asio::io_context> shared_io_context();
asio::io_context& io_context();

} // namespace libera::net
