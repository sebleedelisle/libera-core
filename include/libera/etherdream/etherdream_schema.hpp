#pragma once
// Ether Dream status (20-byte) schema using libera::schema (C++17)

#include <cstdint>
#include <vector>
#include "libera/schema/libera_schema.hpp"

namespace libera::etherdream::schema {

// Short alias for the helpers (optional but tidy)
namespace lsch = ::libera::schema;

// --- Wire model (POD + small enums) ---
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

// Optional flag bits (document as needed)
namespace LightFlags {
    constexpr uint16_t ShutterOpen = 1u << 0;
    constexpr uint16_t InterlockOK = 1u << 1;
}

// --- Validators for enum ranges ---
using LE_Range  = lsch::EnumRange<LightEngineState, 0, 3>;
using PB_Range  = lsch::EnumRange<PlaybackState   , 0, 3>;
using SRC_Range = lsch::EnumRange<Source          , 0, 2>;

// --- Cross-field rules ---
inline const auto statusRules = lsch::objectValidator([](const DacStatus& s)
    -> lsch::expected<void, lsch::DecodeError>
{
    if (s.protocol != 0 && s.protocol != 1)
        return lsch::unexpected<lsch::DecodeError>({"protocol", "unsupported version"});
    if (s.playbackState == PlaybackState::Playing && s.pointRate == 0)
        return lsch::unexpected<lsch::DecodeError>({"pointRate", "zero while playing"});
    return {};
});

// --- Fields (order must match wire) ---
inline const auto dacStatusFields = std::make_tuple(
    lsch::field<&DacStatus::protocol        >("protocol"       , lsch::BeU8{} ),
    lsch::field<&DacStatus::lightEngineState>("lightEngineState", lsch::BeU8{} , LE_Range{}),
    lsch::field<&DacStatus::playbackState   >("playbackState"  , lsch::BeU8{} , PB_Range{}),
    lsch::field<&DacStatus::source          >("source"         , lsch::BeU8{} , SRC_Range{}),
    lsch::field<&DacStatus::lightEngineFlags>("lightEngineFlags", lsch::BeU16{}),
    lsch::field<&DacStatus::playbackFlags   >("playbackFlags"  , lsch::BeU16{}),
    lsch::field<&DacStatus::sourceFlags     >("sourceFlags"    , lsch::BeU16{}),
    lsch::field<&DacStatus::bufferFullness  >("bufferFullness" , lsch::BeU16{}),
    lsch::field<&DacStatus::pointRate       >("pointRate"      , lsch::BeU32{}),
    lsch::field<&DacStatus::pointCount      >("pointCount"     , lsch::BeU32{})
);

// --- The schema object ---
inline const auto dacStatusSchema = lsch::makeSchema<DacStatus>(dacStatusFields, statusRules);

// --- Optional convenience helpers ---
inline lsch::expected<DacStatus, lsch::DecodeError>
decodeStatus(lsch::ByteView view) {
    if (view.size() != 20)
        return lsch::unexpected<lsch::DecodeError>({"packet","expected 20 bytes"});
    return lsch::decode(dacStatusSchema, view);
}

inline lsch::expected<std::vector<std::byte>, lsch::DecodeError>
encodeStatus(const DacStatus& s) {
    return lsch::encode(dacStatusSchema, s);
}

} // namespace libera::etherdream::schema
