// EtherDreamDevice.cpp
#include "libera/etherdream/EtherDreamDevice.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
#include <iomanip>  // for std::setw, std::setfill, std::hex, std::dec
#include <cstddef>  // for std::byte, std::to_integer

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
    constexpr auto tick = 33ms;
    constexpr std::size_t minPointsPerTick = 1000;
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


        core::PointFillRequest req;
        req.minimumPointsRequired = minPointsPerTick;
        req.estimatedFirstPointRenderTime = std::chrono::steady_clock::now() + tick;

        const bool gotPoints = pullOnce(req); // fills `newPoints`, appends to `pointsToSend`

        if (gotPoints) {
            std::cout << "Pulled " << newPoints.size()
                      << " new points. Total buffered: "
                      << pointsToSend.size() << std::endl;
        }

        if (gotPoints && isConnected()) {
            // TODO: serialize & send via tcpClient.write_all(...)
            pointsToSend.clear();
        } else {
            // Not connected - do not let the buffer grow without bound.
            if (pointsToSend.size() > maxBufferedPoints) {
                pointsToSend.clear();
            }
        }

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
