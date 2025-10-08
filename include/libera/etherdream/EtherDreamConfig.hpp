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
constexpr unsigned short ETHERDREAM_DAC_PORT_DEFAULT = 7765;
constexpr std::uint16_t ETHERDREAM_TARGET_POINT_RATE = 30000;

// Streaming behaviour ---------------------------------------------------------
constexpr std::chrono::milliseconds ETHERDREAM_TICK_INTERVAL{33};
constexpr std::size_t ETHERDREAM_BUFFER_CAPACITY = 1799;   // device FIFO depth in points
constexpr std::size_t ETHERDREAM_MIN_PACKET_POINTS = 150;  // minimum batch we want to ship
constexpr std::chrono::milliseconds ETHERDREAM_MIN_SLEEP{1};
constexpr std::chrono::milliseconds ETHERDREAM_MAX_SLEEP{50};

} // namespace libera::etherdream::config
