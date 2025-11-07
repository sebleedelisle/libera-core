#pragma once
#include "libera/core/Expected.hpp"
#include "libera/core/LaserDeviceBase.hpp"
#include "libera/net/NetConfig.hpp"
#include "libera/net/TcpClient.hpp"
#include "libera/etherdream/EtherDreamConfig.hpp"
#include "libera/etherdream/EtherDreamCommand.hpp"
#include "libera/etherdream/EtherDreamResponse.hpp"
#include <memory>
#include <string_view>
#include <optional>
#include <chrono>

namespace libera::etherdream {

using libera::expected;
namespace ip = libera::net::asio::ip;

/**
 * @brief Streaming controller that talks to an EtherDream DAC.
 *
 * The device inherits latency management, worker thread lifecycle, and point
 * buffering from `LaserDeviceBase`. The latency budget exposed by the base
 * class feeds both refill sizing and the per-operation TCP deadlines enforced
 * by `libera::net::TcpClient`.
 *
 * Responsibilities:
 * - Maintain the TCP connection to the DAC.
 * - Poll status frames, decode them via `EtherDreamResponse`, and react.
 * - Request points from the user callback and stream device-formatted frames.
 * - Drive the worker loop supplied by the base class.
 */
class EtherDreamDevice : public libera::core::LaserDeviceBase {
public:
    EtherDreamDevice();
    ~EtherDreamDevice();

    // non-copyable / non-movable
    EtherDreamDevice(const EtherDreamDevice&) = delete;
    EtherDreamDevice& operator=(const EtherDreamDevice&) = delete;
    EtherDreamDevice(EtherDreamDevice&&) = delete;
    EtherDreamDevice& operator=(EtherDreamDevice&&) = delete;

    struct DacAck {
        EtherDreamStatus status{};
        char command = 0;
    };


    /**
     * @brief Connect to the DAC using a resolved IP address.
     * @param address Target address.
     * @param port EtherDream TCP port (defaults to 7765).
     */
    expected<void>
    connect(const ip::address& address,
            unsigned short port = config::ETHERDREAM_DAC_PORT_DEFAULT);

    /**
     * @brief Convenience overload that parses dotted quad strings.
     * @param addressstring IPv4 string (e.g. "192.168.0.50").
     * @param port EtherDream TCP port (defaults to 7765).
     */
    expected<void>
    connect(const std::string& addressstring,
            unsigned short port = config::ETHERDREAM_DAC_PORT_DEFAULT);

    void close();                        // idempotent
    bool isConnected() const;           // const-safe

    
protected:
    void run() override;


private:
   

    /// Wait for the response frame to a specific command.
    expected<DacAck>
    waitForResponse(char command);

    /// Send a single-byte command and synchronously wait for its ACK.
    expected<DacAck>
    sendCommand(char command);

    expected<DacAck>
    sendBeginCommand(std::uint32_t pointRate);

    /// Issue the point-rate command ('q') and return the associated ACK.
    expected<DacAck>
    sendPointRate(std::uint16_t rate);

    std::size_t calculateMinimumPoints();

    long long computeSleepDurationMS();
    void sleepUntilNextPoints();

    void handleNetworkFailure(std::string_view where,
                              const std::error_code& ec);

    void resetPoints();

    double pointsToMillis(std::size_t pointCount,
                          std::uint32_t rate);
    int millisToPoints(double millis,
                       std::uint32_t rate);

    std::uint16_t estimateBufferFullness() const;

    void updatePlaybackRequirements(const EtherDreamStatus& status, bool commandAcked);
    core::PointFillRequest getFillRequest();
    void sendPoints();
    void sendClear();
    void sendPrepare();
    void sendBegin();
    void ensureTargetPointRate();

    EtherDreamStatus lastKnownStatus{};
    std::chrono::steady_clock::time_point lastReceiveTime{};
    libera::net::TcpClient tcpClient;
    std::optional<libera::net::asio::ip::address> rememberedAddress{};
    bool rateChangePending = false;
    bool clearRequired = false;
    bool prepareRequired = false;
    bool beginRequired = false;
    std::size_t minBuffer = 256; // EtherDream 3+ cannot report below this buffer depth.

    bool failureEncountered = false;
};

} // namespace libera::etherdream
