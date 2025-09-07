#pragma once
#include "libera/net/NetConfig.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>

/**
 * with_deadline
 *
 * Starts your async op and a timer on the same executor.
 * Whichever finishes first wins:
 *  - op completes first: cancel timer, return the op's error_code
 *  - timer fires first : cancel op, return operation_aborted
 *
 * IMPORTANT: Your asio::io_context must be running on some thread while
 * this function waits (e.g., run by your app / NetService).
 */
namespace libera::net {

template<typename StartAsync, typename Cancel>
error_code with_deadline(
    asio::any_io_executor ex,
    std::chrono::milliseconds timeout,
    StartAsync start_async,
    Cancel cancel)
{
    asio::steady_timer timer(ex);

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    error_code ec = asio::error::would_block;

    // Completion of the user async op
    auto op_handler = [&](const error_code& op_ec, auto&&... /*ignored*/) {
        {
            std::lock_guard<std::mutex> lk(m);
            if (done) return;           // another path already won
            ec = op_ec;
            done = true;
        }
        cv.notify_one();                // wake waiter first...
        timer.cancel();                 // ...then cancel timer (its handler must be benign)
    };

    // Kick off the async operation (it must call our op_handler)
    start_async(op_handler);

    // Arm the deadline
    timer.expires_after(timeout);
    timer.async_wait([&](const error_code& tec){
        if (tec == asio::error::operation_aborted) {
            // Cancelled because op finished — DO NOT touch m/cv/done here.
            return;
        }
        // Timer really expired first → cancel the op and signal completion
        cancel();
        {
            std::lock_guard<std::mutex> lk(m);
            if (done) return;           // op raced and already finished
            ec = asio::error::operation_aborted;
            done = true;
        }
        cv.notify_one();
    });

    // Wait until either branch completes
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&]{ return done; });
    return ec;
}

}