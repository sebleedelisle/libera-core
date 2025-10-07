#pragma once

#include "libera/etherdream/ByteBuffer.hpp"
#include "libera/core/LaserPoint.hpp"

#include <cstddef>
#include <cstdint>

namespace libera::etherdream {

class EtherDreamCommand {
public:
    void setDataCommand(std::uint16_t pointCount);
    void addPoint(const core::LaserPoint& point, bool setRateChangeFlag);
    void setBeginCommand(std::uint32_t pointRate);
    void setPointRateCommand(std::uint32_t pointRate);

    const std::uint8_t* data() const { return buffer.data(); }
    std::size_t size() const { return buffer.size(); }

private:
    static std::int16_t encodeCoordinate(float value) noexcept;
    static std::uint16_t encodeChannel(float value) noexcept;

    ByteBuffer buffer;
};

} // namespace libera::etherdream
