#pragma once
// Ether Dream status (20-byte) parsing with libera::schema (C++17)

#include <cstdint>
#include <vector>
#include "libera/schema/libera_schema.hpp"

namespace libera::core::etherdream::schema {

// Wire layout is 20 bytes, big-endian for multi-byte fields:
//  0: protocol (u8)
//  1: light_engine_state (u8)
//  2: playback_state (u8)
//  3: source (u8)
//  4..5: light_engine_flags (u16 BE)
//  6..7: playback_flags (u16 BE)
//  8..9: source_flags (u16 BE)
// 10..11: buffer_fullness (u16 BE)
// 12..15: point_rate (u32 BE)
// 16..19: point_count (u32 BE)

// Plain POD model with small enums so it stays C-friendly.
enum class LightEngineState : uint8_t { Idle=0, Armed=1, Warmup=2, Fault=3 };
enum class PlaybackState    : uint8_t { Stopped=0, Prepared=1, Playing=2, Paused=3 };
enum class Source           : uint8_t { Network=0, SDCard=1, Test=2 };

struct DacStatus {
    uint8_t          protocol = 0;
    LightEngineState lightEngineState = LightEngineState::Idle;
    PlaybackState    playbackState    = PlaybackState::Stopped;
    Source           source           = Source::Network;

    uint16_t lightEngineFlags = 0;
    uint16_t playbackFlags    = 0;
    uint16_t sourceFlags      = 0;
    uint16_t bufferFullness   = 0;

    uint32_t pointRate  = 0;
    uint32_t pointCount = 0;
};

// Optional flag bits for readability (document as you learn more)
namespace LightFlags {
    constexpr uint16_t ShutterOpen = 1u << 0;
    constexpr uint16_t InterlockOK = 1u << 1;
    // add more as needed
}

// Validators for enum ranges
using LE_Range  = libera::schema::EnumRange<LightEngineState, 0, 3>;
using PB_Range  = libera::schema::EnumRange<PlaybackState   , 0, 3>;
using SRC_Range = libera::schema::EnumRange<Source          , 0, 2>;

// Cross-field rules:
// - protocol must be 0 or 1
// - if playing, pointRate must be > 0
inline auto statusRules = libera::schema::objectValidator([](const DacStatus& s)
    -> libera::schema::expected<void, libera::schema::DecodeError>
{
    if (s.protocol != 0 && s.protocol != 1) {
        return libera::schema::unexpected<libera::schema::DecodeError>({"protocol", "unsupported version"});
    }
    if (s.playbackState == PlaybackState::Playing && s.pointRate == 0) {
        return libera::schema::unexpected<libera::schema::DecodeError>({"pointRate", "zero while playing"});
    }
    return {};
});

// The schema - order must match the wire exactly
using namespace libera::schema;

constexpr auto dacStatusFields = std::make_tuple(
    field<&DacStatus::protocol        >("protocol"       , BeU8{} , NonZero{}),
    field<&DacStatus::lightEngineState>("lightEngineState", BeU8{} , LE_Range{}),
    field<&DacStatus::playbackState   >("playbackState"  , BeU8{} , PB_Range{}),
    field<&DacStatus::source          >("source"         , BeU8{} , SRC_Range{}),
    field<&DacStatus::lightEngineFlags>("lightEngineFlags", BeU16{}),
    field<&DacStatus::playbackFlags   >("playbackFlags"  , BeU16{}),
    field<&DacStatus::sourceFlags     >("sourceFlags"    , BeU16{}),
    field<&DacStatus::bufferFullness  >("bufferFullness" , BeU16{}),
    field<&DacStatus::pointRate       >("pointRate"      , BeU32{}),
    field<&DacStatus::pointCount      >("pointCount"     , BeU32{})
);

inline const auto dacStatusSchema =
    makeSchema<DacStatus>(dacStatusFields, statusRules);

// Convenience helpers

// Returns true if buffer size equals 20 (strict), or tweak to >= 20 to accept extra trailing bytes.
inline bool isValidStatusFrameSize(std::size_t n) { return n == 20; }

// Build BE bytes from a DacStatus (mainly useful for tests)
inline libera::schema::expected<std::vector<std::byte>, libera::schema::DecodeError>
encodeStatus(const DacStatus& s) {
    return libera::schema::encode(dacStatusSchema, s);
}

// Parse a 20-byte status into a DacStatus
inline libera::schema::expected<DacStatus, libera::schema::DecodeError>
decodeStatus(libera::schema::ByteView view) {
    if (!isValidStatusFrameSize(view.size())) {
        return libera::schema::unexpected<libera::schema::DecodeError>({"packet", "expected 20 bytes"});
    }
    return libera::schema::decode(dacStatusSchema, view);
}

} // namespace etherdream
