// EtherDreamProtocol.hpp
// -----------------------------------------------------------------------------
// Encoding helpers for the EtherDream DAC wire protocol.
// Responsibilities:
//   * Provide protocol constants (command sizes, scaling factors).
//   * Offer fast serialization of high-level LaserPoint values into raw frames.
//   * Keep heavy lifting out of EtherDreamDevice so the worker loop stays clear.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "libera/core/LaserPoint.hpp"

namespace libera::etherdream::protocol {

// Immutable protocol constants exposed so other components can reason about
// EtherDream packet sizing without spelunking the serialization implementation.
constexpr std::size_t ETHERDREAM_HEADER_SIZE = 1 + sizeof(std::uint16_t) * 2; // command + count + flags
constexpr std::size_t ETHERDREAM_POINT_FIELD_COUNT = 9;                        // control + XYZRGBIU1U2
constexpr std::size_t ETHERDREAM_POINT_SIZE = ETHERDREAM_POINT_FIELD_COUNT * sizeof(std::uint16_t);
constexpr float ETHERDREAM_COORD_SCALE = 32767.0f; // per EtherDream spec: signed 16-bit coords
constexpr float ETHERDREAM_CHANNEL_SCALE = 65535.0f; // per EtherDream spec: 16-bit colour/intensity

struct PacketView {
    std::byte* data = nullptr;
    std::size_t size = 0;

    [[nodiscard]] bool empty() const noexcept { return size == 0; }
};

// Serialize a batch of LaserPoint values into EtherDream format. Returns a
// PacketView whose span points at thread-local scratch storage that remains
// valid until the next serializePoints() call on the same thread.
[[nodiscard]] PacketView serializePoints(const std::vector<libera::core::LaserPoint>& points,
                                        bool rateChangeRequested);

} // namespace libera::etherdream::protocol
