#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace libera::etherdream {

class ByteBuffer {
public:
    ByteBuffer();

    void clear();
    void appendChar(char value);
    void appendUInt8(std::uint8_t value);
    void appendUInt16(std::uint16_t value);
    void appendInt16(std::int16_t value);
    void appendUInt32(std::uint32_t value);

    const std::uint8_t* data() const { return buffer.data(); }
    std::uint8_t* data() { return buffer.data(); }
    std::size_t size() const { return buffer.size(); }

private:
    std::vector<std::uint8_t> buffer;
};

} // namespace libera::etherdream
