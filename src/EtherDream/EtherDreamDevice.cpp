// EtherDreamDevice.cpp
// -----------------------------------------------------------------------------
// Orchestrates the EtherDream DAC worker loop: connection handling, periodic
// status polling, point generation, and scheduling of serialized frames.
#include "libera/etherdream/EtherDreamDevice.hpp"

#include "libera/etherdream/EtherDreamConfig.hpp"
#include "libera/etherdream/EtherDreamProtocol.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string_view>
#include <system_error>
#include <thread>
#include <cstddef>  // for std::byte, std::to_integer
#include <cstdint>

using namespace std::chrono_literals; // enable 100ms / 1s literals

namespace libera::etherdream {

using libera::expected;
using libera::unexpected;
namespace ip = libera::net::asio::ip;
namespace asio = libera::net::asio;

EtherDreamDevice::EtherDreamDevice() = default;

EtherDreamDevice::~EtherDreamDevice() {
    // Clean shutdown order:
    // 1) stop worker thread (exits run loop)
    // 2) close TCP (cancels outstanding socket ops)
    stop();
    close();
}

expected<void>
EtherDreamDevice::connect(const ip::address& address, unsigned short port) {

    libera::net::tcp::endpoint endpoint(address, port);

    std::error_code ec = tcpClient.connect(endpoint, latencyMillis);
    if (ec) {
        std::cerr << "[EtherDreamDevice] connect failed: " << ec.message()
                  << " (to " << address.to_string() << ":" << port << ")\n";
        return unexpected(ec);
    }

    tcpClient.setLowLatency(); // low jitter for realtime-ish streams

    rememberedAddress = address;

    std::cout << "[EtherDreamDevice] connected to "
              << address.to_string() << ":" << port << "\n";

    return {};
}

expected<void>
EtherDreamDevice::connect(const std::string& addressstring, unsigned short port) {
    std::error_code ec;
    auto ip = libera::net::asio::ip::make_address(addressstring, ec);
    if (ec) {
        std::cerr << "Invalid IP: " << ec.message() << "\n";
        return unexpected(ec);
    }

    if (auto r = connect(ip, port); !r) {
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
    const auto bufferCapacity = config::ETHERDREAM_BUFFER_CAPACITY;
    const auto minPacketPoints = config::ETHERDREAM_MIN_PACKET_POINTS;
    bool failureEncountered = false;

    if (!tcpClient.is_open()) {
        std::cerr << "[EtherDreamDevice] run() called without an active connection.\n";
        running = false;
        return;
    }

    // Step 1: wait for the initial '?' ACK immediately after connection.
    auto initialAck = waitForResponse('?');
    if (!initialAck) {
        if (auto pingAck = sendCommand('?'); !pingAck) {
            handleFailure("initial ping", pingAck.error(), failureEncountered);
            return;
        } else {
            initialAck = pingAck;
        }
    }
    lastKnownStatus = initialAck->status;

    // if (lastKnownStatus.pointRate != config::ETHERDREAM_TARGET_POINT_RATE) {
    //     if (auto rateAck = setPointRate(config::ETHERDREAM_TARGET_POINT_RATE); !rateAck) {
    //         handleFailure("point rate command", rateAck.error(), failureEncountered);
    //         return;
    //     } else {
    //         lastKnownStatus = rateAck->status;
    //     }
    // }

    if (auto prepareAck = sendCommand('p'); !prepareAck) {
        handleFailure("prepare command", prepareAck.error(), failureEncountered);
        return;
    } else {
        lastKnownStatus = prepareAck->status;
    }

    if (lastKnownStatus.pointRate != config::ETHERDREAM_TARGET_POINT_RATE) {
        if (auto rateAck = setPointRate(config::ETHERDREAM_TARGET_POINT_RATE); !rateAck) {
            handleFailure("point rate command", rateAck.error(), failureEncountered);
            return;
        } else {
            lastKnownStatus = rateAck->status;
        }
    }

    // Main streaming loop.
    while (running) {
        const std::size_t bufferFullness = lastKnownStatus.bufferFullness;
        const std::size_t bufferFree = bufferCapacity > bufferFullness
            ? bufferCapacity - bufferFullness
            : 0;

        const auto latencyBudget = std::chrono::milliseconds{latencyMillis};
        const std::size_t minimumPointsNeeded =
            calculateMinimumPoints(lastKnownStatus, latencyMillis);
        const std::size_t desiredPoints =
            clampDesiredPoints(minimumPointsNeeded, minPacketPoints, bufferFree);

        bool sentFrameThisIteration = false;

        // Step 4: generate and send a packet when the FIFO needs more data.
        if (desiredPoints > 0) {
            pointsToSend.clear();

            core::PointFillRequest req;
            req.minimumPointsRequired = desiredPoints;
            req.maximumPointsRequired = bufferFree;
            req.estimatedFirstPointRenderTime =
                std::chrono::steady_clock::now() + latencyBudget;

            const bool producedPoints = requestPoints(req);
            if (!producedPoints || pointsToSend.size() < desiredPoints) {
                handleFailure("point generation", std::make_error_code(std::errc::no_message_available), failureEncountered);
                break;
            }

            // Cap the packet to avoid overfilling the device FIFO.
            if (pointsToSend.size() > bufferFree) {
                pointsToSend.resize(bufferFree);
            }

            const bool injectRateChange = rateChangePending;
            const auto packet = protocol::serializePoints(pointsToSend, injectRateChange);
            if (packet.empty()) {
                handleFailure("packet serialization", std::make_error_code(std::errc::invalid_argument), failureEncountered);
                break;
            }

            std::cerr << "[EtherDreamDevice] sending 'd' packet: "
                      << pointsToSend.size() << " points, "
                      << packet.size << " bytes\n";

            if (auto ec = tcpClient.write_all(packet.data, packet.size, latencyMillis); ec) {
                handleFailure("stream write", ec, failureEncountered);
                break;
            }

            if (auto dataAck = waitForResponse('d'); !dataAck) {
                handleFailure("waiting for data ACK", dataAck.error(), failureEncountered);
                break;
            } else {
                lastKnownStatus = dataAck->status;
                if (injectRateChange) {
                    rateChangePending = false;
                }
            }

            pointsToSend.clear();
            sentFrameThisIteration = true;
        }

        // Step 5: ensure playback is running.
        if (lastKnownStatus.playbackState != schema::PlaybackState::Playing) {
            if (auto beginAck = sendCommand('b'); !beginAck) {
                handleFailure("begin command", beginAck.error(), failureEncountered);
                break;
            } else {
                lastKnownStatus = beginAck->status;
            }
        }

        const auto sleepDuration = computeSleepDuration(lastKnownStatus, bufferCapacity, minPacketPoints);
        if (sleepDuration.count() > 0) {
            std::this_thread::sleep_for(sleepDuration);
        }

        // Step 7: loop. If we did not send a frame we may want a fresh status snapshot
        // to avoid stale buffer fullness. A lightweight ping keeps the state current.
        if (!sentFrameThisIteration) {
            if (auto statusAck = sendCommand('?'); statusAck) {
                lastKnownStatus = statusAck->status;
            }
        }
    }

    if (!tcpClient.is_open()) {
        return;
    }

    if (!failureEncountered) {
        return;
    }

    close();
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::waitForResponse(char command)
{
    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    const auto timeoutMillis = std::max<long long>(currentLatency, libera::net::default_timeout());

    std::array<std::byte, 22> raw{};
    if (!tcpClient.is_open()) {
        return unexpected(make_error_code(std::errc::not_connected));
    }

    std::size_t bytesTransferred = 0;
    if (auto ec = tcpClient.read_exact(raw.data(), raw.size(), timeoutMillis, &bytesTransferred); ec) {
        std::cerr << "[EtherDreamDevice] read_exact failed after " << timeoutMillis
                  << "ms, transferred " << bytesTransferred << " bytes\n";
        return unexpected(std::error_code(ec.value(), ec.category()));
    }

    const char response = static_cast<char>(raw[0]);
    const char echoedCommand = static_cast<char>(raw[1]);

    auto dumpAck = [&](std::ostream& os) {
        os << "  Raw bytes: ";
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const auto value = static_cast<unsigned char>(raw[i]);
            os << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(value) << ' ';
        }
        os << std::dec << "\n  As chars:  ";
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const auto value = static_cast<unsigned char>(raw[i]);
            os << ((value >= 0x20 && value <= 0x7E) ? static_cast<char>(value) : '.');
        }
        os << '\n';
    };

    std::cout << "[EtherDreamDevice] ACK dump for command '" << command << "'\n";
    dumpAck(std::cout);

    if (response != 'a' || echoedCommand != command) {
        std::cerr << "[EtherDreamDevice] Unexpected ACK sequence. Expected 'a' for command '"
                  << command << "' but received '" << response << "' for '"
                  << echoedCommand << "'.\n";
        dumpAck(std::cerr);
        return unexpected(make_error_code(std::errc::protocol_error));
    }

    auto decoded = schema::decodeStatus(
        libera::schema::ByteView{raw.data() + 2, 20});
    if (!decoded) {
        const auto& e = decoded.error();
        std::cerr << "[EtherDreamDevice] decodeStatus failed at '"
                  << e.where << "': " << e.what << "\n";
        return unexpected(make_error_code(std::errc::protocol_error));
    }

    const auto& st = *decoded;
    std::cout << "[EtherDreamDevice] Decoded status: "
              << "proto=" << static_cast<int>(st.protocol)
              << " playback=" << static_cast<int>(st.playbackState)
              << " buffer=" << st.bufferFullness
              << " rate=" << st.pointRate
              << " pointCount=" << st.pointCount
              << '\n';

    return DacAck{*decoded, echoedCommand};
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::sendCommand(char command) {
    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    const auto timeoutMillis = std::max<long long>(currentLatency, libera::net::default_timeout());
    const uint8_t cmdByte = static_cast<uint8_t>(command);
    if (auto ec = tcpClient.write_all(&cmdByte, 1, timeoutMillis); ec) {
        return unexpected(ec);
    }
    return waitForResponse(command);
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::setPointRate(std::uint16_t rate) {
    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    const auto timeoutMillis = std::max<long long>(currentLatency, libera::net::default_timeout());
    std::array<uint8_t, 3> payload{};
    payload[0] = static_cast<uint8_t>('q');
    payload[1] = static_cast<uint8_t>((rate >> 8) & 0xFFu);
    payload[2] = static_cast<uint8_t>(rate & 0xFFu);

    if (auto ec = tcpClient.write_all(payload.data(), payload.size(), timeoutMillis); ec) {
        if (ec == asio::error::timed_out) {
            std::cerr << "[EtherDreamDevice] point rate write timed out after "
                      << timeoutMillis << "ms\n";
        }
        return unexpected(ec);
    }

    auto ack = waitForResponse('q');
    if (!ack && ack.error() == asio::error::timed_out) {
        std::cerr << "[EtherDreamDevice] point rate ACK timed out after "
                  << timeoutMillis << "ms\n";
    }
    if (ack) {
        rateChangePending = true;
    }
    return ack;
}

std::size_t
EtherDreamDevice::calculateMinimumPoints(const schema::DacStatus& status,
                                         long long maxLatencyMillis) {
    if (status.pointRate == 0 || maxLatencyMillis <= 0) {
        return 0;
    }

    const double requiredPoints =
        (static_cast<double>(status.pointRate) * static_cast<double>(maxLatencyMillis)) / 1000.0;
    if (requiredPoints <= static_cast<double>(status.bufferFullness)) {
        return 0;
    }

    const double deficit = requiredPoints - static_cast<double>(status.bufferFullness);
    return static_cast<std::size_t>(std::ceil(deficit));
}

std::size_t
EtherDreamDevice::clampDesiredPoints(std::size_t minimumPointsNeeded,
                                     std::size_t minPacketPoints,
                                     std::size_t bufferFree) {
    if (bufferFree == 0) {
        return 0;
    }

    const std::size_t baseline = std::max(minimumPointsNeeded, minPacketPoints);
    return std::min(baseline, bufferFree);
}

std::chrono::milliseconds
EtherDreamDevice::computeSleepDuration(const schema::DacStatus& status,
                                       std::size_t bufferCapacity,
                                       std::size_t minPacketPoints) {
    if (status.pointRate == 0) {
        return config::ETHERDREAM_MAX_SLEEP;
    }

    const std::size_t bufferFree = bufferCapacity > status.bufferFullness
        ? bufferCapacity - status.bufferFullness
        : 0;

    if (bufferFree >= minPacketPoints) {
        return config::ETHERDREAM_MIN_SLEEP;
    }

    const std::size_t pointsNeeded = minPacketPoints - bufferFree;
    const double ms = (static_cast<double>(pointsNeeded) * 1000.0)
                    / static_cast<double>(std::max<std::uint32_t>(status.pointRate, 1u));
    auto duration = std::chrono::milliseconds(static_cast<std::int64_t>(std::ceil(ms)));
    return std::clamp(duration, config::ETHERDREAM_MIN_SLEEP, config::ETHERDREAM_MAX_SLEEP);
}

void EtherDreamDevice::handleFailure(std::string_view where,
                                     const std::error_code& ec,
                                     bool& failureEncountered) {
    std::cerr << "[EtherDreamDevice] " << where << " failed: " << ec.message() << "\n";
    running = false;
    failureEncountered = true;
}




} // namespace libera::etherdream
