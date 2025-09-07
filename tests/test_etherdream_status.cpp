// C++17 test for Ether Dream status parsing using libera::schema
// No external test framework needed (uses simple asserts).
//
// Build (g++/clang++):
//   g++ -std=c++17 -Iinclude -Ithirdparty -O2 tests/test_etherdream_status.cpp -o test_status
//
// Build (MSVC):
//   cl /std:c++17 /I include /I thirdparty tests\test_etherdream_status.cpp
//
// Where:
//   include/     contains your "libera/schema/libera_schema.hpp" and "libera/core/etherdream/schema/etherdream_schema.hpp"
//   thirdparty/  contains "tl/expected.hpp"

#include <iostream>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>

#include "libera/schema/libera_schema.hpp"
#include "libera/core/etherdream/etherdream_schema.hpp"

using libera::schema::ByteView;
using libera::schema::DecodeError;
using libera::schema::decode;
using libera::schema::encode;

namespace ED = libera::core::etherdream::schema;

// --- tiny assert helpers ---
static int g_failures = 0;

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { std::cerr << "ASSERT TRUE FAILED: " << (msg) << " @ " << __FILE__ << ":" << __LINE__ << "\n"; ++g_failures; } } while(0)

#define ASSERT_EQ(a,b,msg) \
    do { auto _va=(a); auto _vb=(b); if (!((_va)==(_vb))) { std::cerr << "ASSERT EQ FAILED: " << (msg) << " (" << _va << " != " << _vb << ") @ " << __FILE__ << ":" << __LINE__ << "\n"; ++g_failures; } } while(0)

// --- helpers to build BE packets ---
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

// Build a valid 20-byte Ether Dream status packet
static std::vector<std::byte> makeValid() {
    using LE = ED::LightEngineState;
    using PB = ED::PlaybackState;
    using SRC = ED::Source;

    std::vector<std::byte> raw;
    raw.reserve(20);
    push8(raw, 1);                       // protocol
    push8(raw, uint8_t(LE::Armed));      // lightEngineState
    push8(raw, uint8_t(PB::Playing));    // playbackState
    push8(raw, uint8_t(SRC::Network));   // source
    push16(raw, 0x0003);                 // lightEngineFlags
    push16(raw, 0x0000);                 // playbackFlags
    push16(raw, 0x0000);                 // sourceFlags
    push16(raw, 1024);                   // bufferFullness
    push32(raw, 30000);                  // pointRate
    push32(raw, 123456);                 // pointCount
    return raw;
}

static void testDecodeValid() {
    auto raw = makeValid();
    ByteView view(raw);
    auto res = decode(ED::dacStatusSchema, view);
    ASSERT_TRUE(res.has_value(), "decode valid frame should succeed");
    if (!res) return;

    const auto& s = *res;
    ASSERT_EQ(int(s.protocol), 1, "protocol");
    ASSERT_EQ(int(s.lightEngineState), int(ED::LightEngineState::Armed), "lightEngineState");
    ASSERT_EQ(int(s.playbackState),    int(ED::PlaybackState::Playing),  "playbackState");
    ASSERT_EQ(int(s.source),           int(ED::Source::Network),         "source");
    ASSERT_EQ(s.lightEngineFlags, 0x0003, "lightEngineFlags");
    ASSERT_EQ(s.bufferFullness, 1024, "bufferFullness");
    ASSERT_EQ(s.pointRate, 30000u, "pointRate");
    ASSERT_EQ(s.pointCount, 123456u, "pointCount");

    // Round-trip encode -> decode
    auto enc = encode(ED::dacStatusSchema, s);
    ASSERT_TRUE(enc.has_value(), "encode of valid status should succeed");
    if (enc) {
        ASSERT_EQ(enc->size(), raw.size(), "round-trip size eq");
        ByteView view2(*enc);
        auto res2 = decode(ED::dacStatusSchema, view2);
        ASSERT_TRUE(res2.has_value(), "round-trip decode should succeed");
    }
}

static void testWrongSize() {
    auto raw = makeValid();
    raw.pop_back(); // 19 bytes instead of 20
    ByteView view(raw);
    // If your decode requires exactly 20 bytes, it should fail early; if you tolerate >=20, adjust the expectation.
    auto res = decode(ED::dacStatusSchema, view);
    ASSERT_TRUE(!res.has_value(), "decode wrong-size should fail");
    if (!res) std::cerr << "Expected fail (wrong size): " << res.error().where << " - " << res.error().what << "\n";
}

static void testEnumOutOfRange() {
    auto raw = makeValid();
    raw[2] = std::byte(255); // playbackState = 255 (invalid)
    ByteView view(raw);
    auto res = decode(ED::dacStatusSchema, view);
    ASSERT_TRUE(!res.has_value(), "decode with invalid enum should fail");
    if (!res) std::cerr << "Expected fail (enum OOR): " << res.error().where << " - " << res.error().what << "\n";
}

static void testPlayingZeroRate() {
    auto raw = makeValid();
    // Set pointRate to zero to trigger object-level rule when state==Playing
    raw[12] = std::byte(0); raw[13] = std::byte(0); raw[14] = std::byte(0); raw[15] = std::byte(0);
    ByteView view(raw);
    auto res = decode(ED::dacStatusSchema, view);
    ASSERT_TRUE(!res.has_value(), "decode should fail when Playing with zero pointRate");
    if (!res) std::cerr << "Expected fail (rule): " << res.error().where << " - " << res.error().what << "\n";
}

static void testUnsupportedProtocol() {
    auto raw = makeValid();
    raw[0] = std::byte(7); // protocol 7 (unsupported in our rule)
    ByteView view(raw);
    auto res = decode(ED::dacStatusSchema, view);
    ASSERT_TRUE(!res.has_value(), "decode should fail for unsupported protocol");
    if (!res) std::cerr << "Expected fail (protocol): " << res.error().where << " - " << res.error().what << "\n";
}

int main() {
    testDecodeValid();
    testWrongSize();
    testEnumOutOfRange();
    testPlayingZeroRate();
    testUnsupportedProtocol();

    if (g_failures) {
        std::cerr << "\nTESTS FAILED: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All Ether Dream status tests passed.\n";
    return 0;
}
