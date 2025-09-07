#pragma once
#include "net/NetConfig.hpp"
#include <thread>

namespace libera::net {
namespace asio = boost::asio;

/**
 * NetService
 *
 * - Owns a boost::asio::io_context (the central event loop for network ops).
 * - Keeps it alive with a work_guard (so it doesn’t exit when idle).
 * - Runs it on a background thread.
 *
 * Typically you create ONE of these in your application
 * and pass it by reference to all your DAC/network classes.
 */
class NetService {
public:
    NetService()
    : io_()
    , work_guard_(asio::make_work_guard(io_)) // keep io_context running
    , t_([this]{ io_.run(); })                // launch worker thread
    {}

    ~NetService() {
        // Tell work_guard we’re done, stop the context, and join thread
        work_guard_.reset();
        io_.stop();
        if (t_.joinable()) t_.join();
    }

    // Provide access to the io_context
    asio::io_context& io() { return io_; }

private:
    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::thread t_;
};

} // namespace libera::net
