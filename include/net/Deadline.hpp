#pragma once
#include "net/NetConfig.hpp"
#include <chrono>

namespace libera::net {
namespace asio = boost::asio;
using namespace std::chrono;

/**
 * with_deadline
 *
 * - Starts an async operation (e.g. async_connect, async_read, async_write).
 * - Starts a timer at the same time.
 * - Whichever finishes first "wins":
 *     - If the operation finishes, we cancel the timer.
 *     - If the timer expires, we cancel the operation.
 *
 * This gives us a portable timeout wrapper.
 */
template<typename StartAsync, typename Cancel>
boost::system::error_code with_deadline(
    asio::any_io_executor ex,      // executor from a socket or strand
    milliseconds timeout,          // e.g. 1000ms
    StartAsync start_async,        // lambda that calls async_* function
    Cancel cancel)                 // lambda that cancels the op
{
    asio::steady_timer timer(ex);              // timer runs on same executor
    boost::system::error_code op_ec = asio::error::would_block;
    bool completed = false;

    // Start the async operation. When done, mark completed + stop timer.
    start_async([&](const boost::system::error_code& ec){
        op_ec = ec;
        completed = true;
        timer.cancel(); // stop the timer
    });

    // Arm the timer. If it fires, cancel the operation.
    timer.expires_after(timeout);
    timer.async_wait([&](const boost::system::error_code&){
        if (!completed) cancel();
    });

    // Pump the io_context until either the op or the timer completes.
    auto* ctx = &asio::query(ex, asio::execution::context)
                    .template get<asio::io_context&>();
    while (!completed && !ctx->stopped()) {
        ctx->run_one(); // process one handler (either op or timer)
    }
    ctx->restart();
    ctx->poll(); // clear any leftover handlers

    return op_ec;
}

} // namespace libera::net
