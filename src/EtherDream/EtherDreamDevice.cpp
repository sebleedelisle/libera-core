// EtherDreamDevice.cpp
// -----------------------------------------------------------------------------
// Orchestrates the EtherDream DAC worker loop: connection handling, periodic
// status polling, point generation, and scheduling of serialized frames.
#include "libera/etherdream/EtherDreamDevice.hpp"

#include "libera/etherdream/EtherDreamConfig.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <cstddef>  // for std::byte, std::to_integer
#include <cstdint>
#include <cctype>

using namespace std::chrono_literals; // enable 100ms / 1s literals

namespace libera::etherdream {

using libera::expected;
using libera::unexpected;
namespace ip = libera::net::asio::ip;
namespace asio = libera::net::asio;

namespace {

const char* toString(LightEngineState state) {
    switch (state) {
        case LightEngineState::Ready:   return "ready";
        case LightEngineState::Warmup:  return "warmup";
        case LightEngineState::Cooldown:return "cooldown";
        case LightEngineState::Estop:   return "estop";
    }
    return "unknown";
}

const char* toString(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle:     return "idle";
        case PlaybackState::Prepared: return "prepared";
        case PlaybackState::Playing:  return "playing";
        case PlaybackState::Paused:   return "paused";
    }
    return "unknown";
}

std::string toHexLine(const std::uint8_t* data, std::size_t size) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        if (i) os << ' ';
        os << std::setw(2) << static_cast<int>(data[i]);
    }
    return os.str();
}

std::string formatStatus(const EtherDreamStatus& status) {
    std::ostringstream os;
    os << "light=" << toString(status.lightEngineState)
       << " playback=" << toString(status.playbackState)
       << " buffer=" << status.bufferFullness
       << " rate=" << status.pointRate
       << " count=" << status.pointCount
       << " flags{L=0x" << std::hex << std::uppercase << status.lightEngineFlags
       << " P=0x" << status.playbackFlags
       << " S=0x" << status.sourceFlags << std::dec << std::nouppercase << "}";
    return os.str();
}

} // namespace

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



    {
        std::cerr << "\n[EtherDream] setup: ensure idle state\n";

        if (lastKnownStatus.playbackState != PlaybackState::Idle) {
            std::cerr << "  playback=" << toString(lastKnownStatus.playbackState)
                      << " -> send 's'\n";
            if (auto stopAck = sendCommand('s'); !stopAck) {
                handleFailure("stop command", stopAck.error(), failureEncountered);
                return;
            } else {
                lastKnownStatus = stopAck->status;
            }
        } else {
            std::cerr << "  already idle; skip 's'\n";
        }

        const bool estop = lastKnownStatus.lightEngineState == LightEngineState::Estop;
        const bool underflow = (lastKnownStatus.playbackFlags & 0x04u) != 0;
        if (estop || underflow) {
            std::cerr << "  clear required (estop=" << (estop ? 'Y' : 'N')
                      << ", underflow=" << (underflow ? 'Y' : 'N') << ") -> send 'c'\n";
            if (auto clearAck = sendCommand('c'); !clearAck) {
                handleFailure("clear command", clearAck.error(), failureEncountered);
                return;
            } else {
                lastKnownStatus = clearAck->status;
            }
        }
    }

    // Main streaming loop.
    std::size_t idlePollCounter = 0;
    while (running) {
        if (lastKnownStatus.playbackState == PlaybackState::Idle) {
            std::cerr << "[EtherDream] playback idle -> send 'p'\n";
            if (auto prepareAck = sendCommand('p'); !prepareAck) {
                handleFailure("prepare command", prepareAck.error(), failureEncountered);
                break;
            } else {
                lastKnownStatus = prepareAck->status;
            }
        }

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
                handleFailure("point generation", std::make_error_code(std::errc::no_message), failureEncountered);
                break;
            }

            // Cap the packet to avoid overfilling the device FIFO.
            if (pointsToSend.size() > bufferFree) {
                pointsToSend.resize(bufferFree);
            }

            const bool injectRateChange = rateChangePending;
            EtherDreamCommand command;
            const std::size_t pointCount = std::min<std::size_t>(pointsToSend.size(), std::numeric_limits<std::uint16_t>::max());
            command.setDataCommand(static_cast<std::uint16_t>(pointCount));
            for (std::size_t idx = 0; idx < pointCount; ++idx) {
                const bool setRateBit = injectRateChange && idx == 0;
                command.addPoint(pointsToSend[idx], setRateBit);
            }

            if (command.size() == 0) {
                handleFailure("packet serialization", std::make_error_code(std::errc::invalid_argument), failureEncountered);
                break;
            }

            std::cerr << "[EtherDream] TX data: points=" << pointsToSend.size()
                      << " bytes=" << command.size() << "\n";

            if (auto ec = tcpClient.write_all(command.data(), command.size(), latencyMillis); ec) {
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
        if (lastKnownStatus.playbackState != PlaybackState::Playing) {
            if (auto beginAck = sendBegin(config::ETHERDREAM_TARGET_POINT_RATE); !beginAck) {
                handleFailure("begin command", beginAck.error(), failureEncountered);
                break;
            } else {
                lastKnownStatus = beginAck->status;
            }
        } else if (lastKnownStatus.pointRate != config::ETHERDREAM_TARGET_POINT_RATE) {
            if (auto rateAck = setPointRate(config::ETHERDREAM_TARGET_POINT_RATE); !rateAck) {
                handleFailure("point rate command", rateAck.error(), failureEncountered);
                break;
            } else {
                lastKnownStatus = rateAck->status;
            }
        }

        const auto sleepDuration = computeSleepDuration(lastKnownStatus, bufferCapacity, minPacketPoints);
        if (sleepDuration.count() > 0) {
            std::this_thread::sleep_for(sleepDuration);
        }

        // Step 7: loop. If we did not send a frame we may want a fresh status snapshot
        // to avoid stale buffer fullness. A lightweight ping keeps the state current.
        if (!sentFrameThisIteration) {
            ++idlePollCounter;
            if (idlePollCounter >= 10) {
                if (auto statusAck = sendCommand('?'); statusAck) {
                    lastKnownStatus = statusAck->status;
                }
                idlePollCounter = 0;
            }
        } else {
            idlePollCounter = 0;
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

    std::array<std::uint8_t, 22> raw{};
    if (!tcpClient.is_open()) {
        return unexpected(make_error_code(std::errc::not_connected));
    }

    std::size_t bytesTransferred = 0;
    if (auto ec = tcpClient.read_exact(raw.data(), raw.size(), timeoutMillis, &bytesTransferred); ec) {
        std::cerr << "[EtherDream] RX timeout after " << timeoutMillis
                  << "ms (" << bytesTransferred << " byte(s) received)\n";
        return unexpected(std::error_code(ec.value(), ec.category()));
    }

    EtherDreamResponse response;
    if (!response.decode(raw.data(), raw.size())) {
        std::cerr << "[EtherDreamDevice] Failed to decode ACK for command '" << command << "'\n";
        return unexpected(make_error_code(std::errc::protocol_error));
    }

    std::cout << "[EtherDream] RX '" << static_cast<char>(response.response)
              << "' for '" << command << "' | " << formatStatus(response.status) << '\n'
              << "           hex: " << toHexLine(raw.data(), raw.size()) << '\n';

    if (response.response != 'a' || static_cast<char>(response.command) != command) {
        std::cerr << "[EtherDream] unexpected ACK: expected 'a' for '" << command
                  << "' but got '" << static_cast<char>(response.response)
                  << "' for '" << static_cast<char>(response.command) << "'\n"
                  << "           hex: " << toHexLine(raw.data(), raw.size()) << '\n';
        return unexpected(make_error_code(std::errc::protocol_error));
    }

    return DacAck{response.status, static_cast<char>(response.command)};
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::sendCommand(char command) {
    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    const auto timeoutMillis = std::max<long long>(currentLatency, libera::net::default_timeout());
    const uint8_t cmdByte = static_cast<uint8_t>(command);
    std::cerr << "[EtherDream] TX '" << command << "' (timeout " << timeoutMillis << "ms)\n";
    if (auto ec = tcpClient.write_all(&cmdByte, 1, timeoutMillis); ec) {
        return unexpected(ec);
    }
    return waitForResponse(command);
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::sendBegin(std::uint32_t pointRate) {
    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    const auto timeoutMillis = std::max<long long>(currentLatency, libera::net::default_timeout());

    EtherDreamCommand command;
    command.setBeginCommand(pointRate);

    std::cerr << "[EtherDream] TX 'b' (rate=" << pointRate
              << ", timeout " << timeoutMillis << "ms)\n";

    if (auto ec = tcpClient.write_all(command.data(), command.size(), timeoutMillis); ec) {
        if (ec == asio::error::timed_out) {
            std::cerr << "[EtherDream] begin write timeout after "
                      << timeoutMillis << "ms\n";
        }
        return unexpected(ec);
    }

    auto ack = waitForResponse('b');
    if (!ack && ack.error() == asio::error::timed_out) {
        std::cerr << "[EtherDream] begin ACK timed out after "
                  << timeoutMillis << "ms\n";
    }
    return ack;
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::setPointRate(std::uint16_t rate) {
    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    const auto timeoutMillis = std::max<long long>(currentLatency, libera::net::default_timeout());
    EtherDreamCommand command;
    command.setPointRateCommand(static_cast<std::uint32_t>(rate));

    std::cerr << "[EtherDream] TX 'q' (rate=" << rate
              << ", timeout " << timeoutMillis << "ms)\n";

    if (auto ec = tcpClient.write_all(command.data(), command.size(), timeoutMillis); ec) {
        if (ec == asio::error::timed_out) {
            std::cerr << "[EtherDream] point-rate write timeout after "
                      << timeoutMillis << "ms\n";
        }
        return unexpected(ec);
    }

    auto ack = waitForResponse('q');
    if (!ack && ack.error() == asio::error::timed_out) {
        std::cerr << "[EtherDream] point-rate ACK timed out after "
                  << timeoutMillis << "ms\n";
    }
    if (ack) {
        rateChangePending = true;
    }
    return ack;
}

std::size_t
EtherDreamDevice::calculateMinimumPoints(const EtherDreamStatus& status,
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
EtherDreamDevice::computeSleepDuration(const EtherDreamStatus& status,
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
