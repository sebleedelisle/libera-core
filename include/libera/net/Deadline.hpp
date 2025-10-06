#pragma once
#include "libera/net/NetConfig.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <memory>

/**
 * with_deadline
 *
 * Pattern:
 * - Start an async operation and an `asio::steady_timer` on the same executor.
 * - Whichever completes first cancels the other and signals a condition
 *   variable so this call can return a result synchronously with a timeout.
 *
 * Why it’s useful:
 * - In openFrameworks you might reach for blocking calls with timeouts; with
 *   Asio, a common pattern is: async + timer + cancel. This utility wraps that
 *   into a concise helper that returns a `std::error_code`.
 *
 * Safety notes (subtle C++/Asio details):
 * - The completion handlers capture a `shared_ptr<State>` so they never access
 *   destroyed mutex/condition_variable even if they run after this function
 *   returns. This avoids a common use-after-free race in naive implementations.
 * - `cancel()` must cancel the same socket/timer that started the operation; we
 *   pass it in from the caller so it can call `socket.cancel()` or similar.
 *
 * Requirements:
 * - Your `asio::io_context` must be running (e.g., via NetService) while we
 *   block waiting. If it is not, this function would wait forever.
 */
namespace libera::net {

template<typename StartAsync, typename Cancel>
std::error_code with_deadline(
    asio::any_io_executor ex,
    std::chrono::milliseconds timeout,
    StartAsync start_async,
    Cancel cancel)
{
    struct State {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        std::error_code ec = asio::error::would_block;
    };

    auto st = std::make_shared<State>();
    auto timer = std::make_shared<asio::steady_timer>(ex);

    // Completion of the user async op
    auto op_handler = [st, timer](const std::error_code& op_ec, auto&&... /*ignored*/) {
        {
            std::lock_guard<std::mutex> lk(st->m);
            if (st->done) return;           // another path already won
            st->ec = op_ec;
            st->done = true;
        }
        st->cv.notify_one();                // wake waiter first...
        timer->cancel();                    // ...then cancel timer (handler must be benign)
    };

    // Kick off the async operation (it must call our op_handler)
    start_async(op_handler);

    // Arm the deadline
    timer->expires_after(timeout);
    timer->async_wait([st, cancel, timer](const std::error_code& tec){
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
