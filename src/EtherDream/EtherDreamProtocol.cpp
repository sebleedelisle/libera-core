// EtherDreamProtocol.cpp
// -----------------------------------------------------------------------------
// Implements the raw EtherDream DAC serialization helpers declared in
// EtherDreamProtocol.hpp. Keeping this here keeps EtherDreamDevice.cpp focused
// on connection management and scheduling logic.

#include "libera/etherdream/EtherDreamProtocol.hpp"

#include <algorithm>
#include <cassert>
#include <type_traits>

namespace {

inline std::vector<std::byte>& packetBuffer() {
    // Thread-local so each streaming thread keeps its own reusable slab.
    static thread_local std::vector<std::byte> buffer;
    return buffer;
}

inline float clampFast(float value, float lo, float hi) noexcept {
    return std::min(std::max(value, lo), hi);
}

inline std::int32_t fastRound(float value) noexcept {
    return static_cast<std::int32_t>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

// Spec ref: Ether Dream DAC v2 documentation, section 2.2 "Point Format".
inline std::uint16_t encodeCoordinate(float value) noexcept {
    const float scaled = clampFast(value, -1.0f, 1.0f) * libera::etherdream::protocol::ETHERDREAM_COORD_SCALE;
    const auto signedWord = static_cast<std::int16_t>(fastRound(scaled));
    return static_cast<std::uint16_t>(signedWord);
}

// Spec ref: Ether Dream DAC v2 documentation, section 2.2 "Point Format".
inline std::uint16_t encodeChannel(float value) noexcept {
    auto scaled = fastRound(clampFast(value, 0.0f, 1.0f) * libera::etherdream::protocol::ETHERDREAM_CHANNEL_SCALE);
    const std::int32_t channelMax = static_cast<std::int32_t>(libera::etherdream::protocol::ETHERDREAM_CHANNEL_SCALE);
    if (scaled < 0) scaled = 0;
    if (scaled > channelMax) scaled = channelMax;
    return static_cast<std::uint16_t>(scaled);
}

inline void write_u8(std::byte*& dst, std::byte value) noexcept {
    *dst++ = value;
}

inline void write_u8(std::byte*& dst, unsigned char value) noexcept {
    write_u8(dst, std::byte{value});
}

inline void write_u8(std::byte*& dst, char value) noexcept {
    write_u8(dst, static_cast<unsigned char>(value));
}

inline void write_le16(std::byte*& dst, std::uint16_t value) noexcept {
    *dst++ = std::byte(value & 0xFFu);
    *dst++ = std::byte((value >> 8) & 0xFFu);
}

template <typename Integral,
          typename = std::enable_if_t<(std::is_integral<Integral>::value && sizeof(Integral) > 2)>>
inline void write_le16(std::byte*& dst, Integral value) noexcept {
    write_le16(dst, static_cast<std::uint16_t>(value));
}

} // namespace

namespace libera::etherdream::protocol {

PacketView serializePoints(const std::vector<libera::core::LaserPoint>& points,
                          bool rateChangeRequested) {
    auto& packet = packetBuffer();

    if (points.empty()) {
        packet.clear();
        return PacketView{};
    }

    packet.resize(ETHERDREAM_HEADER_SIZE + points.size() * ETHERDREAM_POINT_SIZE);

    auto* out = packet.data();
    write_u8(out, 'd'); // EtherDream "data" command (Per Ether Dream DAC v2 spec section 2.1)

    write_le16(out, points.size());
    write_le16(out, 0); // flags currently unused (little-endian per spec)

    bool rateFlagPending = rateChangeRequested;

    for (const auto& pt : points) {
        const std::uint16_t control = rateFlagPending ? 0x8000u : 0u;
        rateFlagPending = false;

        write_le16(out, control);
        write_le16(out, encodeCoordinate(pt.x));
        write_le16(out, encodeCoordinate(pt.y));
        write_le16(out, encodeChannel(pt.r));
        write_le16(out, encodeChannel(pt.g));
        write_le16(out, encodeChannel(pt.b));
        write_le16(out, encodeChannel(pt.i));
        write_le16(out, encodeChannel(pt.u1));
        write_le16(out, encodeChannel(pt.u2));
    }

    return PacketView{packet.data(), packet.size()};
}

} // namespace libera::etherdream::protocol
