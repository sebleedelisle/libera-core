#pragma once
#include "libera/net/NetConfig.hpp"
#include <thread>
#include <iostream>

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
 * - `asio::io_context` is the event loop for all async operations.
 * - `executor_work_guard` prevents the loop from exiting when idle.
 * - A background `std::thread` calls `io_.run()` until destruction.
 *
 * Lifetime and shutdown:
 * - Destroy network-using objects (controllers, sockets) before NetService so
 *   their async handlers are cancelled/finished while the io_context still runs.
 * - In the destructor we reset the work guard, call `stop()`, and join the
 *   thread—this is the orderly shutdown sequence recommended by Asio.
 */
class NetService {
public:
    NetService()
    : io_()
    , work_guard_(asio::make_work_guard(io_)) // keep io_context running
    , t_([this]{ io_.run(); })                // launch worker thread
    {
        std::cout << "Creating NetService object"<< std::endl; 
    }

    ~NetService() {
        // Tell work_guard we’re done, stop the context, and join thread
        work_guard_.reset();
        io_.stop();
        if (t_.joinable()) t_.join();
    }

    NetService(const NetService&) = delete;
    NetService& operator=(const NetService&) = delete;
    NetService(NetService&&) = delete;
    NetService& operator=(NetService&&) = delete;


    // Provide access to the io_context
    asio::io_context& io() { return io_; }
    // If you need an executor (to bind timers/sockets) use io().get_executor().
    // We keep it simple here and pass around references to io_context directly.

private:
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::thread t_;
};

} // namespace libera::net
