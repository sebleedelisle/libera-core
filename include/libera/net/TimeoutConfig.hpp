#pragma once

#include <chrono>

namespace libera::net {

/**
 * @brief Stores and exposes a default timeout for networking operations.
 *
 * Helpers such as `TcpClient` fall back to this value when callers do not
 * provide an explicit timeout.
 */
inline long long& default_timeout_storage() {
    static long long timeout{1000}; // sensible default = 1s
    return timeout;
}

inline void set_default_timeout(std::chrono::milliseconds timeout) {
    default_timeout_storage() = timeout.count();
}

inline void set_default_timeout_ms(long long timeout_ms) {
    default_timeout_storage() = timeout_ms;
}

inline long long default_timeout() {
    return default_timeout_storage();
}

} // namespace libera::net
