#pragma once

#include "libera/core/ByteBuffer.hpp"
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
    void setSingleByteCommand(char opcode);

    const std::uint8_t* data() const { return buffer.data(); }
    std::size_t size() const { return buffer.size(); }
    bool isReady() const { return opcode != 0 && !bufferEmpty(); }
    char commandOpcode() const { return opcode; }
    void reset();

private:
    bool bufferEmpty() const { return buffer.size() == 0; }
    static std::int16_t encodeCoordinate(float value) noexcept;
    static std::uint16_t encodeChannel(float value) noexcept;

    core::ByteBuffer buffer;
    char opcode = 0;
};

} // namespace libera::etherdream
