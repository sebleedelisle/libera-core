/**
 * @brief Implements the EtherDream DAC worker loop: connection, polling, and streaming.
 */
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

using namespace std::chrono_literals; // Enable 100ms / 1s literals.

namespace libera::etherdream {

using libera::expected;
using libera::unexpected;
using DacAck = EtherDreamDevice::DacAck;
namespace ip = libera::net::asio::ip;
namespace asio = libera::net::asio;

EtherDreamDevice::EtherDreamDevice() = default;

EtherDreamDevice::~EtherDreamDevice() {
    // Orderly shutdown: stop the worker thread and close the TCP connection.
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

    tcpClient.setLowLatency(); // Enable low jitter for realtime-ish streams.

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
    std::cout << "[EtherDreamDevice] close()\n"; 
    // Keep the operation idempotent so repeated calls are harmless.
    if (!tcpClient.is_open()) {
        rememberedAddress.reset();
        return;
    }
    // Future improvement: cancel outstanding async operations before closing.
    tcpClient.close();
    rememberedAddress.reset();
}

bool EtherDreamDevice::isConnected() const {
    return tcpClient.is_open();
}



void EtherDreamDevice::run() {

    failureEncountered = false;

    if (!tcpClient.is_open()) {
        std::cerr << "[EtherDreamDevice] run() called without an active connection.\n";
        running = false;
        return;
    }

    auto initialAck = waitForResponse('?');
    if (!initialAck) {
        if (auto pingAck = sendCommand('?'); !pingAck) {
            handleNetworkFailure("initial ping", pingAck.error());
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

        sleepUntilNextPoints();

        auto req = getFillRequest();
        if (req.needsPoints(config::ETHERDREAM_MIN_PACKET_POINTS)) {
            requestPoints(req);
            sendPoints();
        }

        if (beginRequired) {
            sendBegin();
        }
    }

    if (!tcpClient.is_open() || !failureEncountered) {
        return;
    }

    close();
}

expected<DacAck>
EtherDreamDevice::waitForResponse(char command) {
    if (!running) {
        return unexpected(std::make_error_code(std::errc::operation_canceled));
    }
    if (!tcpClient.is_open()) {
        return unexpected(make_error_code(std::errc::not_connected));
    }

    const long long timeoutMillis = latencyMillis;

    // Local buffer sized for one ACK payload (22 bytes).
    std::array<std::uint8_t, 22> raw{};

    std::size_t bytesTransferred = 0;
    if (auto ec = tcpClient.read_exact(raw.data(), raw.size(), timeoutMillis, &bytesTransferred); ec) {
        std::cerr << "[EtherDream] RX error "
            << ec.value() << ' '
            << ec.category().name() << " - "
            << ec.message() << '\n';
        return unexpected(std::error_code(ec.value(), ec.category()));
    }

    EtherDreamResponse response;
    if (!response.decode(raw.data(), raw.size())) {
        std::cerr << "[EtherDreamDevice] Failed to decode ACK for command '" << command << "'\n";
        return unexpected(make_error_code(std::errc::protocol_error));
    }

    const bool ackMatched = (response.response == 'a') &&
                            (static_cast<char>(response.command) == command);

    // Update begin/clear/prepare flags based on the latest status frame.
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

expected<DacAck>
EtherDreamDevice::sendCommand(char command) {

    if (!running) {
        return unexpected(std::make_error_code(std::errc::operation_canceled));
    }

    const long long timeoutMillis = latencyMillis;
    
    const uint8_t cmdByte = static_cast<uint8_t>(command);
    std::cerr << "[EtherDream] TX '" << command << "' (timeout " << timeoutMillis << "ms)\n";
    if (auto ec = tcpClient.write_all(&cmdByte, 1, timeoutMillis); ec) {
        return unexpected(ec);
    }
    return waitForResponse(command);
}

expected<DacAck>
EtherDreamDevice::sendBeginCommand(std::uint32_t pointRate) {

    const long long timeoutMillis = latencyMillis;

    EtherDreamCommand command;
    command.setBeginCommand(pointRate);

    std::cout << "[EtherDream] TX 'b' (rate=" << pointRate
              << ", timeout " << timeoutMillis << "ms)\n";

    if (auto ec = tcpClient.write_all(command.data(), command.size(), timeoutMillis); ec) {
        if (ec == asio::error::timed_out) {
            std::cerr << "[EtherDream] begin write timeout after "
                      << timeoutMillis << "ms\n";
        }
        return unexpected(ec);
    }

    return waitForResponse('b');
}

expected<DacAck>
EtherDreamDevice::sendPointRate(std::uint16_t rate) {
    
    const long long timeoutMillis = latencyMillis;

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
EtherDreamDevice::calculateMinimumPoints() {

    const auto latency = latencyMillis.load(std::memory_order_relaxed);


    if (lastKnownStatus.pointRate == 0 || latency <= 0) {
        return 0;
    }

    const auto bufferFullness = estimateBufferFullness();
    double requiredPoints = minBuffer + (
        (static_cast<double>(lastKnownStatus.pointRate) * static_cast<double>(latency)) / 1000.0);
    if (requiredPoints <= static_cast<double>(bufferFullness)) {
        return 0;
    }

    if(requiredPoints>config::ETHERDREAM_BUFFER_CAPACITY) requiredPoints = config::ETHERDREAM_BUFFER_CAPACITY; 

    const double deficit = requiredPoints - static_cast<double>(bufferFullness);

    return static_cast<std::size_t>(std::ceil(deficit));
}


long long
EtherDreamDevice::computeSleepDurationMS() {
    // Compute how many points must remain queued to satisfy the latency budget.
    const auto latency = latencyMillis.load(std::memory_order_relaxed);
    if (latency <= 0 || lastKnownStatus.pointRate == 0) {
        return 0;
    }

    const int minPointsInBuffer = millisToPoints(latency, lastKnownStatus.pointRate);

    // Estimate how long until the buffer drains to that minimum.
    const auto fullness = estimateBufferFullness();
    const int deficit = static_cast<int>(fullness) - minPointsInBuffer;
    const int pointsToWait = std::min<int>(config::ETHERDREAM_MIN_PACKET_POINTS,
                                        std::max(deficit, 0));

    // Cap sleeps at 5 ms to keep responsiveness high.
    return static_cast<long long>(std::min<long long>(
        5,
        pointsToMillis(pointsToWait, lastKnownStatus.pointRate)));
}

void EtherDreamDevice::handleNetworkFailure(std::string_view where,
                                     const std::error_code& ec) {
    std::cerr << "[EtherDreamDevice] " << where << " failed: " << ec.message() << "\n";
    running = false;
    failureEncountered = true;
}


void EtherDreamDevice::updatePlaybackRequirements(const EtherDreamStatus& status, bool commandAcked) {
    lastKnownStatus = status;
    lastReceiveTime = std::chrono::steady_clock::now();
    

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
    const auto bufferFullness = estimateBufferFullness();

    const auto bufferCapacity = config::ETHERDREAM_BUFFER_CAPACITY;
    const auto freeSpace = bufferCapacity > bufferFullness ? bufferCapacity - bufferFullness : 0;
    const auto minimumPointsRequired =
    std::min<std::size_t>(calculateMinimumPoints(), freeSpace);
    
   
    const long long latencyMs = latencyMillis.load(std::memory_order_relaxed);

    core::PointFillRequest req;
    req.maximumPointsRequired = freeSpace;
    req.minimumPointsRequired = minimumPointsRequired;
    req.estimatedFirstPointRenderTime =
        std::chrono::steady_clock::now() + std::chrono::milliseconds{latencyMs}; // TODO: incorporate remaining buffer delay.
    std::cout << "[EtherDreamDevice Point fill request "<< req.minimumPointsRequired << " " << req.maximumPointsRequired << std::endl; 

    pointsToSend.clear();
    return req;
}

void EtherDreamDevice::sendPoints() {
    if (clearRequired || prepareRequired) {
        resetPoints();
        return;
    }

    if (pointsToSend.size() == 0) {
        return;
    }

    const bool injectRateChange = rateChangePending;
    EtherDreamCommand command;
    const std::uint16_t pointCount =  static_cast<std::uint16_t>(pointsToSend.size());

    command.setDataCommand(pointCount);

    for (std::size_t idx = 0; idx < pointCount; ++idx) {
        const bool setRateBit = injectRateChange && idx == 0;
        command.addPoint(pointsToSend[idx], setRateBit);
    }

    if (command.size() == 0) {
        handleNetworkFailure("packet serialization", std::make_error_code(std::errc::invalid_argument));
        resetPoints();
        return;
    }

    std::cerr << "[EtherDream] TX data: points=" << pointsToSend.size()
              << " bytes=" << command.size() << "\n";

    const auto timeoutMS = latencyMillis.load(std::memory_order_relaxed);

    if (auto ec = tcpClient.write_all(command.data(), command.size(), timeoutMS); ec) {
        handleNetworkFailure("stream write", ec);
        resetPoints();
        return;
    }

    auto dataAck = waitForResponse('d');
    if (!dataAck) {
        handleNetworkFailure("waiting for data ACK", dataAck.error());
        resetPoints();
        return;
    }

    if (injectRateChange) {
        rateChangePending = false;
    }

    resetPoints();
}

void EtherDreamDevice::sendClear() {
    std::cerr << "[EtherDream] clear required -> send 'c'\n";
    if (auto ack = sendCommand('c'); !ack) {
        handleNetworkFailure("clear command", ack.error());
    }
}

void EtherDreamDevice::sendPrepare() {
    std::cerr << "[EtherDream] prepare required -> send 'p'\n";
    if (auto ack = sendCommand('p'); !ack) {
        handleNetworkFailure("prepare command", ack.error());
    }
}

void EtherDreamDevice::sendBegin() {
    std::cerr << "[EtherDream] begin required -> send 'b'\n";
    if (auto ack = sendBeginCommand(config::ETHERDREAM_TARGET_POINT_RATE); !ack) {
        handleNetworkFailure("begin command", ack.error());
    }
}

std::uint16_t EtherDreamDevice::estimateBufferFullness() const {
    const auto rate = lastKnownStatus.pointRate;
    if (rate == 0) {
        return lastKnownStatus.bufferFullness;
    }

    if (lastReceiveTime == std::chrono::steady_clock::time_point{}) {
        return lastKnownStatus.bufferFullness;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = now - lastReceiveTime;
    if (elapsed <= std::chrono::steady_clock::duration::zero()) {
        return lastKnownStatus.bufferFullness;
    }

    const auto elapsedUs =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    if (elapsedUs <= 0) {
        return lastKnownStatus.bufferFullness;
    }

    const double consumed =
        (static_cast<double>(rate) * static_cast<double>(elapsedUs)) / 1'000'000.0;
    const double estimated =
        static_cast<double>(lastKnownStatus.bufferFullness) - consumed;
    const double clamped =
        std::clamp(estimated, 0.0, static_cast<double>(config::ETHERDREAM_BUFFER_CAPACITY));

    return static_cast<std::uint16_t>(std::llround(clamped));
}

void EtherDreamDevice::ensureTargetPointRate() {
    if (clearRequired || prepareRequired || beginRequired) {
        return;
    }

    if (lastKnownStatus.playbackState == PlaybackState::Playing &&
        lastKnownStatus.pointRate != config::ETHERDREAM_TARGET_POINT_RATE) {
        if (auto rateAck = sendPointRate(config::ETHERDREAM_TARGET_POINT_RATE); !rateAck) {
            handleNetworkFailure("point rate command", rateAck.error());
        }
    }
}

void EtherDreamDevice::sleepUntilNextPoints() {
    long long durationMS = computeSleepDurationMS();
    std::cout << "[EtherDreamDevice] Sleeping for " << durationMS << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds{durationMS});
}

void EtherDreamDevice::resetPoints() {
    pointsToSend.clear();
}

double
EtherDreamDevice::pointsToMillis(std::size_t pointCount, std::uint32_t rate) {
    if (rate == 0 || pointCount == 0) {
        return 0; 
    }

    const double millis =
        (static_cast<double>(pointCount) * 1000.0) / static_cast<double>(rate);

    return std::max(millis, 0.0);
}

int EtherDreamDevice::millisToPoints(double millis, std::uint32_t rate) {
    if (rate == 0 || millis <= 0.0) {
        return 0;
    }

    const double seconds = millis / 1000.0;
    const double rawPoints = seconds * static_cast<double>(rate);
    const auto rounded = static_cast<long long>(std::llround(rawPoints));

    if (rounded <= 0) {
        return 0;
    }

    return static_cast<int>(std::min<long long>(rounded, std::numeric_limits<int>::max()));
}

} // namespace libera::etherdream
