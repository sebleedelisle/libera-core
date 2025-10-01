// EtherDreamDevice.cpp
#include "libera/etherdream/EtherDreamDevice.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#include <iomanip>  // for std::setw, std::setfill, std::hex, std::dec
#include <cstddef>  // for std::byte, std::to_integer
#include <cstdint>
#include <vector>

namespace {

//--------------------------------------------------------------------------
// Immutable protocol constants used by every EtherDream frame we emit.
//--------------------------------------------------------------------------
constexpr std::size_t kEtherDreamHeaderSize = 1 + sizeof(std::uint16_t) * 2; // command + count + flags
constexpr std::size_t kEtherDreamPointFieldCount = 9;                        // control + XYZRGBIU1U2
constexpr std::size_t kEtherDreamPointSize = kEtherDreamPointFieldCount * sizeof(std::uint16_t);

//--------------------------------------------------------------------------
// Return a thread-local packet buffer so each worker thread reuses its own
// memory slab. This avoids heap churn without needing external plumbing.
//--------------------------------------------------------------------------
inline std::vector<std::byte>& packetBuffer() {
    static thread_local std::vector<std::byte> buffer;
    return buffer;
}

//--------------------------------------------------------------------------
// Extremely small helpers kept inline so the compiler can fold them away.
//--------------------------------------------------------------------------
inline void write_be16(std::byte*& dst, std::uint16_t value) noexcept {
    *dst++ = std::byte((value >> 8) & 0xFFu);
    *dst++ = std::byte(value & 0xFFu);
}

inline float clampFast(float v, float lo, float hi) noexcept {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline std::int32_t fastRound(float v) noexcept {
    return static_cast<std::int32_t>(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

inline std::uint16_t encodeCoordinate(float v) noexcept {
    constexpr float kCoordScale = 32767.0f; // EtherDream expects signed 16-bit coordinates
    const float scaled = clampFast(v, -1.0f, 1.0f) * kCoordScale;
    const auto signedWord = static_cast<std::int16_t>(fastRound(scaled));
    return static_cast<std::uint16_t>(signedWord);
}

inline std::uint16_t encodeChannel(float v) noexcept {
    constexpr float kChannelScale = 65535.0f;
    auto scaled = fastRound(clampFast(v, 0.0f, 1.0f) * kChannelScale);
    if (scaled < 0) scaled = 0;
    if (scaled > 65535) scaled = 65535;
    return static_cast<std::uint16_t>(scaled);
}

//--------------------------------------------------------------------------
// Serialize a batch of LaserPoint records into the thread-local packet.
// Returns the valid byte count ready for transmission (0 when no data).
//--------------------------------------------------------------------------
inline std::size_t serializePoints(const std::vector<libera::core::LaserPoint>& points,
                                   std::vector<std::byte>& packet) {
    if (points.empty()) {
        packet.clear(); // keep capacity for next tick but signal no payload
        return 0;
    }

    packet.resize(kEtherDreamHeaderSize + points.size() * kEtherDreamPointSize);

    auto* out = packet.data();
    *out++ = std::byte{'d'}; // "data" command identifier per EtherDream spec

    write_be16(out, static_cast<std::uint16_t>(points.size()));
    write_be16(out, 0); // no flags for now

    for (const auto& pt : points) {
        write_be16(out, 0); // control bits remain zero until we add future features
        write_be16(out, encodeCoordinate(pt.x));
        write_be16(out, encodeCoordinate(pt.y));
        write_be16(out, encodeChannel(pt.r));
        write_be16(out, encodeChannel(pt.g));
        write_be16(out, encodeChannel(pt.b));
        write_be16(out, encodeChannel(pt.i));
        write_be16(out, encodeChannel(pt.u1));
        write_be16(out, encodeChannel(pt.u2));
    }

    return packet.size();
}

} // namespace

using namespace std::chrono_literals; // enable 100ms / 1s literals

namespace libera::etherdream {

EtherDreamDevice::EtherDreamDevice(libera::net::asio::io_context& ioContext)
: tcpClient(ioContext) {}

EtherDreamDevice::~EtherDreamDevice() {
    // Clean shutdown order:
    // 1) stop worker thread (exits run loop)
    // 2) close TCP (cancels outstanding socket ops)
    stop();
    close();
}

tl::expected<void, std::error_code>
EtherDreamDevice::connect(const libera::net::asio::ip::address& address) {

    constexpr unsigned short port = 7765;
    libera::net::tcp::endpoint endpoint(address, port);

    libera::net::error_code ec = tcpClient.connect(std::array{endpoint}, 1s);
    if (ec) {
        std::cerr << "[EtherDreamDevice] connect failed: " << ec.message()
                  << " (to " << address.to_string() << ":" << port << ")\n";
        return tl::unexpected(std::error_code(ec.value(), ec.category()));
    }

    tcpClient.setLowLatency(); // low jitter for realtime-ish streams

    rememberedAddress = address;

    std::cout << "[EtherDreamDevice] connected to "
              << address.to_string() << ":" << port << "\n";

    return {};
}

tl::expected<void, std::error_code>
EtherDreamDevice::connect(const std::string& addressstring) {
    libera::net::error_code ec;
    auto ip = libera::net::asio::ip::make_address(addressstring, ec);
    if (ec) {
        std::cerr << "Invalid IP: " << ec.message() << "\n";
        return tl::unexpected(std::error_code(ec.value(), ec.category()));
    }

    if (auto r = connect(ip); !r) {
        std::cerr << "Connect failed: " << r.error().message() << "\n";
        return r;
    }

    return {};
}


void EtherDreamDevice::close() {
    // Make this idempotent and keep internal state consistent.
    if (!tcpClient.is_open()) {
        rememberedAddress.reset();
        return;
    }
    // If you add async ops later, call tcpClient.cancel() here first.
    tcpClient.close();
    rememberedAddress.reset();
}

bool EtherDreamDevice::isConnected() const {
    // Prefer TcpClient::is_open() const if you can add it, to avoid const_cast.
    return tcpClient.is_open();
}

void EtherDreamDevice::run() {
    // We tick at ~30 Hz which keeps up with real hardware without hogging CPU.
    constexpr auto tick = 33ms;
    // Minimum batch size we insist on so the DAC FIFO stays comfortably primed.
    constexpr std::size_t minPointsPerTick = 1000;
    // Safety valve: never let the staging buffer grow unbounded if the link drops.
    constexpr std::size_t maxBufferedPoints = 30000; // cap to avoid runaway

    // Small wire-up test: the EtherDream protocol responds to '?' with
    // an ACK ('a') plus a 20-byte status. This helps verify connectivity.
    std::cout << "Sending ping" << std::endl;
    uint8_t cmd = '?';
    libera::net::error_code ec = tcpClient.write_all(&cmd, 1, std::chrono::milliseconds(100));
    if(ec) { 
        std::cerr << "Error sending ping " << ec.message() << std::endl;
    }
  
    while (running) {
        if (isConnected()) {
            if (auto st = read_status(100ms); st) {
                // use st->buffer_fullness, st->playback_state, etc.
                std::cout << "Received response " << (int)(st->playbackState) << std::endl;
            } else {
                // decode or IO error - up to you whether to close/retry/log
                std::cerr << st.error().message() << "\n";
            }
        }


        // Build the request describing how many fresh points we need right now.
        core::PointFillRequest req;
        req.minimumPointsRequired = minPointsPerTick;
        req.estimatedFirstPointRenderTime = std::chrono::steady_clock::now() + tick;

        // Ask the client callback for fresh geometry; this appends into pointsToSend.
        const bool gotPoints = pullOnce(req); // fills `newPoints`, appends to `pointsToSend`

        if (gotPoints) {
            std::cout << "Pulled " << newPoints.size()
                      << " new points. Total buffered: "
                      << pointsToSend.size() << std::endl;
        }

        if (gotPoints && isConnected()) {
            if (!pointsToSend.empty()) {
                // Serialize into a per-thread buffer, then push the whole payload in one sys-call.
                auto& packet = packetBuffer();
                const std::size_t bytesToSend = serializePoints(pointsToSend, packet);

                // Only attempt IO when we actually generated payload bytes.
                if (bytesToSend > 0) {
                    if (auto ec = tcpClient.write_all(packet.data(), bytesToSend, 100ms); ec) {
                        std::cerr << "[EtherDreamDevice] write_all failed: " << ec.message() << "\n";
                    } else {
                        // Drop points once we know the DAC accepted the frame.
                        pointsToSend.clear();
                    }
                }
            }
        } else {
            // Not connected - do not let the buffer grow without bound.
            if (pointsToSend.size() > maxBufferedPoints) {
                pointsToSend.clear();
            }
        }

        // Sleep until the next frame boundary to honor the controller cadence.
        std::this_thread::sleep_for(tick);
    }
}
tl::expected<schema::DacStatus, std::error_code>
EtherDreamDevice::read_status(std::chrono::milliseconds timeout)
{
    std::array<std::byte, 22> raw{};

    if (!tcpClient.is_open()) {
        return tl::unexpected(make_error_code(std::errc::not_connected));
    }

    libera::net::error_code ec = tcpClient.read_exact(raw.data(), raw.size(), timeout);
    if (ec) {
        return tl::unexpected(std::error_code(ec.value(), ec.category()));
    }

    // Debug dump
    std::cout << "read 22 bytes: ";
    for (auto b : raw) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << std::to_integer<int>(b) << " ";
    }
    std::cout << std::dec << std::endl;

    // First two bytes = response + command
    uint8_t response = static_cast<uint8_t>(raw[0]);
    uint8_t command  = static_cast<uint8_t>(raw[1]);

    if (response != 'a') {
        std::cerr << "[EtherDreamDevice] non-ACK response: "
                  << static_cast<char>(response)
                  << " to command " << static_cast<char>(command) << "\n";
        return tl::unexpected(make_error_code(std::errc::protocol_error));
    }

    // Remaining 20 bytes = dac_status
    auto decoded = schema::decodeStatus(
        libera::schema::ByteView{raw.data() + 2, 20});
    if (!decoded) {
        const auto& e = decoded.error();
        std::cerr << "[EtherDreamDevice] decodeStatus failed at '"
                  << e.where << "': " << e.what << "\n";
        return tl::unexpected(make_error_code(std::errc::protocol_error));
    }

    return *decoded;
}




} // namespace libera::etherdream
