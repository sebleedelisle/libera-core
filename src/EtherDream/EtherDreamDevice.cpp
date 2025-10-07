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
#include <limits>
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

    return connect(ip, port);
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

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::waitForResponse(char command)
{
    if (!running) {
        return unexpected(std::make_error_code(std::errc::operation_canceled));
    }

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

    const bool ackMatched = (response.response == 'a') &&
                            (static_cast<char>(response.command) == command);
    updatePlaybackRequirements(response.status, ackMatched);

    std::cout << "[EtherDream] RX '" << static_cast<char>(response.response)
              << "' for '" << command << "' | " << response.status.describe() << '\n'
              << "           hex: " << EtherDreamStatus::toHexLine(raw.data(), raw.size()) << '\n';

    if (!ackMatched) {
        std::cerr << "[EtherDream] unexpected ACK: expected 'a' for '" << command
                  << "' but got '" << static_cast<char>(response.response)
                  << "' for '" << static_cast<char>(response.command) << "'\n"
                  << "           hex: " << EtherDreamStatus::toHexLine(raw.data(), raw.size()) << '\n';
        return unexpected(make_error_code(std::errc::protocol_error));
    }

    return DacAck{response.status, static_cast<char>(response.command)};
}

expected<EtherDreamDevice::DacAck>
EtherDreamDevice::sendCommand(char command) {
    if (!running) {
        return unexpected(std::make_error_code(std::errc::operation_canceled));
    }

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
EtherDreamDevice::sendBeginCommand(std::uint32_t pointRate) {
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
                                     const std::error_code& ec) {
    std::cerr << "[EtherDreamDevice] " << where << " failed: " << ec.message() << "\n";
    running = false;
    failureEncountered = true;
}
void EtherDreamDevice::run() {

    failureEncountered = false;
    idlePollCounter = 0;
    pendingDesiredPoints = 0;
    pendingBufferFree = 0;

    if (!tcpClient.is_open()) {
        std::cerr << "[EtherDreamDevice] run() called without an active connection.\n";
        running = false;
        return;
    }

    auto initialAck = waitForResponse('?');
    if (!initialAck) {
        if (auto pingAck = sendCommand('?'); !pingAck) {
            handleFailure("initial ping", pingAck.error());
            return;
        }
    }
 
    while (running) {
        if (clearRequired) {
            sendClear();
        }

        if (prepareRequired) {
            sendPrepare();
        }

        auto req = getFillRequest();
        if (pendingDesiredPoints > 0) {
            requestPoints(req);
        }
        sendPoints();

        if (beginRequired) {
            sendBegin();
        }
    }

    if (!tcpClient.is_open() || !failureEncountered) {
        return;
    }

    close();
}

void EtherDreamDevice::updatePlaybackRequirements(const EtherDreamStatus& status, bool commandAcked) {
    lastKnownStatus = status;

    const bool estop = status.lightEngineState == LightEngineState::Estop;
    const bool underflow = (status.playbackFlags & 0x04u) != 0;
    clearRequired = estop || underflow || !commandAcked;

    prepareRequired = !clearRequired
        && status.lightEngineState == LightEngineState::Ready
        && status.playbackState == PlaybackState::Idle;

    const std::size_t bufferFullness = static_cast<std::size_t>(status.bufferFullness);
    beginRequired = !clearRequired
        && status.playbackState == PlaybackState::Prepared
        && bufferFullness >= config::ETHERDREAM_MIN_PACKET_POINTS;
}

core::PointFillRequest EtherDreamDevice::getFillRequest() {
    pendingDesiredPoints = 0;
    pendingBufferFree = 0;

    const auto bufferCapacity = config::ETHERDREAM_BUFFER_CAPACITY;
    const std::size_t bufferFullness = static_cast<std::size_t>(lastKnownStatus.bufferFullness);
    pendingBufferFree = bufferCapacity > bufferFullness
        ? bufferCapacity - bufferFullness
        : 0;

    const long long latencyMs = latencyMillis.load(std::memory_order_relaxed);
    const std::size_t minimumPointsNeeded = calculateMinimumPoints(lastKnownStatus, latencyMs);
    pendingDesiredPoints = clampDesiredPoints(
        minimumPointsNeeded,
        config::ETHERDREAM_MIN_PACKET_POINTS,
        pendingBufferFree);

    core::PointFillRequest req;
    req.minimumPointsRequired = pendingDesiredPoints;
    req.maximumPointsRequired = pendingBufferFree;
    req.estimatedFirstPointRenderTime =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{latencyMs};

    pointsToSend.clear();
    return req;
}

void EtherDreamDevice::sendPoints() {
    bool sentFrame = false;

    auto resetContext = [&] {
        pendingDesiredPoints = 0;
        pendingBufferFree = 0;
        pointsToSend.clear();
    };

    if (clearRequired || prepareRequired) {
        resetContext();
        handlePostIteration(false);
        return;
    }

    if (pendingDesiredPoints == 0) {
        resetContext();
        handlePostIteration(false);
        return;
    }

    if (pointsToSend.size() < pendingDesiredPoints) {
        handleFailure("point generation", std::make_error_code(std::errc::no_message));
        resetContext();
        return;
    }

    if (pointsToSend.size() > pendingBufferFree) {
        pointsToSend.resize(pendingBufferFree);
    }

    const bool injectRateChange = rateChangePending;
    EtherDreamCommand command;
    const std::size_t pointCount = std::min<std::size_t>(
        pointsToSend.size(),
        static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()));
    command.setDataCommand(static_cast<std::uint16_t>(pointCount));
    for (std::size_t idx = 0; idx < pointCount; ++idx) {
        const bool setRateBit = injectRateChange && idx == 0;
        command.addPoint(pointsToSend[idx], setRateBit);
    }

    if (command.size() == 0) {
        handleFailure("packet serialization", std::make_error_code(std::errc::invalid_argument));
        resetContext();
        return;
    }

    std::cerr << "[EtherDream] TX data: points=" << pointsToSend.size()
              << " bytes=" << command.size() << "\n";

    const auto currentLatency = latencyMillis.load(std::memory_order_relaxed);
    if (auto ec = tcpClient.write_all(command.data(), command.size(), currentLatency); ec) {
        handleFailure("stream write", ec);
        resetContext();
        return;
    }

    auto dataAck = waitForResponse('d');
    if (!dataAck) {
        handleFailure("waiting for data ACK", dataAck.error());
        resetContext();
        return;
    }

    if (injectRateChange) {
        rateChangePending = false;
    }

    sentFrame = true;
    resetContext();
    handlePostIteration(sentFrame);
}

void EtherDreamDevice::sendClear() {
    std::cerr << "[EtherDream] clear required -> send 'c'\n";
    if (auto ack = sendCommand('c'); !ack) {
        handleFailure("clear command", ack.error());
    }
}

void EtherDreamDevice::sendPrepare() {
    std::cerr << "[EtherDream] prepare required -> send 'p'\n";
    if (auto ack = sendCommand('p'); !ack) {
        handleFailure("prepare command", ack.error());
    }
}

void EtherDreamDevice::sendBegin() {
    std::cerr << "[EtherDream] begin required -> send 'b'\n";
    if (auto ack = sendBeginCommand(config::ETHERDREAM_TARGET_POINT_RATE); !ack) {
        handleFailure("begin command", ack.error());
    }
}

void EtherDreamDevice::handlePostIteration(bool sentFrame) {
    if (!running) {
        return;
    }

    if (sentFrame) {
        idlePollCounter = 0;
    } else {
        ++idlePollCounter;
        if (idlePollCounter >= 10) {
            if (auto statusAck = sendCommand('?'); !statusAck) {
                handleFailure("status poll", statusAck.error());
                return;
            }
            idlePollCounter = 0;
        }
    }

    ensureTargetPointRate();

    const auto sleepDuration = computeSleepDuration(
        lastKnownStatus,
        config::ETHERDREAM_BUFFER_CAPACITY,
        config::ETHERDREAM_MIN_PACKET_POINTS);
    if (sleepDuration.count() > 0) {
        std::this_thread::sleep_for(sleepDuration);
    }
}

void EtherDreamDevice::ensureTargetPointRate() {
    if (clearRequired || prepareRequired || beginRequired) {
        return;
    }

    if (lastKnownStatus.playbackState == PlaybackState::Playing &&
        lastKnownStatus.pointRate != config::ETHERDREAM_TARGET_POINT_RATE) {
        if (auto rateAck = setPointRate(config::ETHERDREAM_TARGET_POINT_RATE); !rateAck) {
            handleFailure("point rate command", rateAck.error());
        }
    }
}

} // namespace libera::etherdream
