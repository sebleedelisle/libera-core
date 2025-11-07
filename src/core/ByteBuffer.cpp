#include "libera/core/ByteBuffer.hpp"

#include <stdexcept>

namespace libera::core {

ByteBuffer::ByteBuffer() {
    buffer.reserve(1024 * 32); // 32 KB to start, grows automatically
}

void ByteBuffer::clear() {
    buffer.clear();
}

void ByteBuffer::appendChar(char value) {
    buffer.push_back(static_cast<std::uint8_t>(value));
}

void ByteBuffer::appendUInt8(std::uint8_t value) {
    buffer.push_back(value);
}

void ByteBuffer::appendUInt16(std::uint16_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void ByteBuffer::appendInt16(std::int16_t value) {
    auto unsignedValue = static_cast<std::uint16_t>(value);
    appendUInt16(unsignedValue);
}

void ByteBuffer::appendUInt32(std::uint32_t value) {
    buffer.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

} // namespace libera::core
