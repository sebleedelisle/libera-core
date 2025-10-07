#pragma once

#include <cstddef>
#include <cstdint>

namespace libera::etherdream {

enum class LightEngineState : std::uint8_t {
    Ready = 0,
    Warmup = 1,
    Cooldown = 2,
    Estop = 3
};

enum class PlaybackState : std::uint8_t {
    Idle = 0,
    Prepared = 1,
    Playing = 2,
    Paused = 3
};

struct EtherDreamStatus {
    std::uint8_t protocol = 0;
    LightEngineState lightEngineState = LightEngineState::Ready;
    PlaybackState playbackState = PlaybackState::Idle;
    std::uint8_t source = 0;
    std::uint16_t lightEngineFlags = 0;
    std::uint16_t playbackFlags = 0;
    std::uint16_t sourceFlags = 0;
    std::uint16_t bufferFullness = 0;
    std::uint32_t pointRate = 0;
    std::uint32_t pointCount = 0;
};

struct EtherDreamResponse {
    std::uint8_t response = 0;
    std::uint8_t command = 0;
    EtherDreamStatus status{};

    bool decode(const std::uint8_t* data, std::size_t size);
};

} // namespace libera::etherdream
