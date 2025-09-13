#pragma once
#include "libera/net/NetConfig.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>

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
    struct State {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        error_code ec = asio::error::would_block;
    };

    auto st = std::make_shared<State>();
    asio::steady_timer timer(ex);

    // Completion of the user async op
    auto op_handler = [st, &timer](const error_code& op_ec, auto&&... /*ignored*/) {
        {
            std::lock_guard<std::mutex> lk(st->m);
            if (st->done) return;           // another path already won
            st->ec = op_ec;
            st->done = true;
        }
        st->cv.notify_one();                // wake waiter first...
        timer.cancel();                     // ...then cancel timer (handler must be benign)
    };

    // Kick off the async operation (it must call our op_handler)
    start_async(op_handler);

    // Arm the deadline
    timer.expires_after(timeout);
    timer.async_wait([st, cancel](const error_code& tec){
        if (tec == asio::error::operation_aborted) {
            // Cancelled because op finished — do nothing.
            return;
        }
        // Timer really expired first → cancel the op and signal completion
        cancel();
        {
            std::lock_guard<std::mutex> lk(st->m);
            if (st->done) return;           // op raced and already finished
            st->ec = asio::error::operation_aborted;
            st->done = true;
        }
        st->cv.notify_one();
    });

    // Wait until either branch completes
    std::unique_lock<std::mutex> lk(st->m);
    st->cv.wait(lk, [&]{ return st->done; });
    return st->ec;
}

}
