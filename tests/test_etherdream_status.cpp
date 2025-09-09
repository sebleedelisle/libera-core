// tests/test_etherdream_status.cpp
// Build (g++/clang++):
//   g++ -std=c++17 -Iinclude -Ithirdparty -O2 tests/test_etherdream_status.cpp -o test_status
// Where:
//   include/     -> libera/schema/libera_schema.hpp
//                  libera/etherdream/schema/etherdream_schema.hpp
//   thirdparty/  -> tl/expected.hpp

#include <iostream>
#include <vector>
#include <cstddef>
#include <cstdint>

#include "libera/schema/libera_schema.hpp"
#include "libera/etherdream/etherdream_schema.hpp"

namespace lsch = ::libera::schema;
namespace ED   = ::libera::etherdream::schema;

// --- tiny assert helpers (no external test framework) ---
static int g_failures = 0;

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { std::cerr << "ASSERT TRUE FAILED: " << (msg) \
        << "  @ " << __FILE__ << ":" << __LINE__ << "\n"; ++g_failures; } } while(0)

#define ASSERT_EQ(a,b,msg) \
    do { auto _va=(a); auto _vb=(b); if (!((_va)==(_vb))) { std::cerr << "ASSERT EQ FAILED: " << (msg) \
        << "  (" << +_va << " != " << +_vb << ")" \
        << "  @ " << __FILE__ << ":" << __LINE__ << "\n"; ++g_failures; } } while(0)

// --- BE helpers for crafting packets in tests ---
static void push8 (std::vector<std::byte>& v, uint8_t x){ v.push_back(static_cast<std::byte>(x)); }
static void push16(std::vector<std::byte>& v, uint16_t x){
    v.push_back(std::byte((x >> 8) & 0xFF));
    v.push_back(std::byte(x & 0xFF));
}
static void push32(std::vector<std::byte>& v, uint32_t x){
    v.push_back(std::byte((x >> 24) & 0xFF));
    v.push_back(std::byte((x >> 16) & 0xFF));
    v.push_back(std::byte((x >> 8 ) & 0xFF));
    v.push_back(std::byte(x & 0xFF));
}

// --- build a valid Ether Dream status packet (20 bytes) ---
static std::vector<std::byte> makeValid() {
    std::vector<std::byte> raw; raw.reserve(20);
    push8 (raw, 1); // protocol
    push8 (raw, uint8_t(ED::LightEngineState::Armed));
    push8 (raw, uint8_t(ED::PlaybackState::Playing));
    push8 (raw, uint8_t(ED::Source::Network));
    push16(raw, ED::LightFlags::ShutterOpen | ED::LightFlags::InterlockOK);
    push16(raw, 0);         // playbackFlags
    push16(raw, 0);         // sourceFlags
    push16(raw, 1024);      // bufferFullness
    push32(raw, 30000);     // pointRate
    push32(raw, 123456);    // pointCount
    return raw;
}

static void testDecodeValid() {
    auto raw = makeValid();
    auto st  = ED::decodeStatus(lsch::ByteView(raw));
    ASSERT_TRUE(st.has_value(), "decodeStatus(valid) succeeds");
    if (!st) return;

    ASSERT_EQ(int(st->protocol), 1, "protocol");
    ASSERT_EQ(int(st->lightEngineState), int(ED::LightEngineState::Armed),   "lightEngineState");
    ASSERT_EQ(int(st->playbackState),    int(ED::PlaybackState::Playing),    "playbackState");
    ASSERT_EQ(int(st->source),           int(ED::Source::Network),           "source");
    ASSERT_EQ(st->lightEngineFlags, 0x0003, "lightEngineFlags");
    ASSERT_EQ(st->bufferFullness, 1024, "bufferFullness");
    ASSERT_EQ(st->pointRate, 30000u, "pointRate");
    ASSERT_EQ(st->pointCount, 123456u, "pointCount");

    // Encode -> decode round-trip
    auto enc = ED::encodeStatus(*st);
    ASSERT_TRUE(enc.has_value(), "encode(valid) succeeds");
    if (!enc) return;
    ASSERT_EQ(enc->size(), raw.size(), "round-trip size matches");

    auto st2 = ED::decodeStatus(lsch::ByteView(*enc));
    ASSERT_TRUE(st2.has_value(), "round-trip decode succeeds");
}

static void testWrongSize() {
    auto raw = makeValid();
    raw.pop_back(); // 19 bytes
    auto st = ED::decodeStatus(lsch::ByteView(raw));
    ASSERT_TRUE(!st.has_value(), "decodeStatus(wrong-size) fails");
    if (!st) std::cerr << "Expected fail (wrong size): " << st.error().where << " - " << st.error().what << "\n";
}

static void testEnumOutOfRange() {
    auto raw = makeValid();
    raw[2] = std::byte(255); // playbackState = 255 (invalid by EnumRange)
    auto st = lsch::decode(ED::dacStatusSchema, lsch::ByteView(raw));
    ASSERT_TRUE(!st.has_value(), "decode(invalid enum) fails");
    if (!st) std::cerr << "Expected fail (enum OOR): " << st.error().where << " - " << st.error().what << "\n";
}

static void testPlayingZeroRate() {
    auto raw = makeValid();
    // Zero pointRate to trip the object-level rule (when Playing)
    raw[12] = std::byte(0); raw[13] = std::byte(0); raw[14] = std::byte(0); raw[15] = std::byte(0);
    auto st = lsch::decode(ED::dacStatusSchema, lsch::ByteView(raw));
    ASSERT_TRUE(!st.has_value(), "decode(Playing with zero rate) fails");
    if (!st) std::cerr << "Expected fail (rule): " << st.error().where << " - " << st.error().what << "\n";
}

static void testProtocolNonZeroValidator() {
    auto raw = makeValid();
    raw[0] = std::byte(0); // field-level NonZero on protocol should trip
    auto st = lsch::decode(ED::dacStatusSchema, lsch::ByteView(raw));
    ASSERT_TRUE(!st.has_value(), "decode(protocol==0) fails NonZero validator");
    if (!st) std::cerr << "Expected fail (NonZero): " << st.error().where << " - " << st.error().what << "\n";
}

int main() {
    testDecodeValid();
    testWrongSize();
    testEnumOutOfRange();
    testPlayingZeroRate();
    testProtocolNonZeroValidator();

    if (g_failures) {
        std::cerr << "\nTESTS FAILED: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All Ether Dream status tests passed.\n";
    return 0;
}
