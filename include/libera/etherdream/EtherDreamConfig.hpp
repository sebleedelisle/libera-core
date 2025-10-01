// EtherDreamConfig.hpp
// -----------------------------------------------------------------------------
// Centralized constants that describe timing and buffer configuration for the
// EtherDream device integration. Keeping them here prevents "magic numbers"
// from drifting across translation units.

#pragma once

#include <chrono>
#include <cstddef>

namespace libera::etherdream::config {

// Networking -----------------------------------------------------------------
constexpr unsigned short ETHERDREAM_DAC_PORT = 7765;
constexpr std::chrono::milliseconds ETHERDREAM_DEFAULT_TIMEOUT{100};
constexpr std::chrono::milliseconds ETHERDREAM_CONNECT_TIMEOUT{1000};

// Scheduling -----------------------------------------------------------------
constexpr std::chrono::milliseconds ETHERDREAM_TICK_INTERVAL{33};
constexpr std::size_t ETHERDREAM_MIN_POINTS_PER_TICK = 1000;
constexpr std::size_t ETHERDREAM_MAX_BUFFERED_POINTS = 30000;

} // namespace libera::etherdream::config
