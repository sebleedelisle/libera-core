// TimeoutConfig.hpp
// -----------------------------------------------------------------------------
// Provides a library-wide default timeout for networking operations. Individual
// calls can still override the value, but helpers like TcpClient will fall back
// to this shared configuration when no timeout argument is supplied.

#pragma once

#include <chrono>

namespace libera::net {

inline std::chrono::milliseconds& default_timeout_storage() {
    static std::chrono::milliseconds timeout{std::chrono::milliseconds{1000}}; // sensible default = 1s
    return timeout;
}

inline void set_default_timeout(std::chrono::milliseconds timeout) {
    default_timeout_storage() = timeout;
}

inline std::chrono::milliseconds default_timeout() {
    return default_timeout_storage();
}

} // namespace libera::net

