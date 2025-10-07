

// EtherDream controller — high-level notes
//
// Responsibilities:
// 1) Connect to the DAC over TCP.
// 2) Periodically poll status (ACK + 20-byte dac_status), decode it, and react.
// 3) Ask the user callback for more points and transmit them in device format.
// 4) Manage its own worker loop (derived from LaserDeviceBase::run()).
//
// Design choices for clarity:
// - Networking is delegated to `libera::net::TcpClient` for timeouts/cancellation.
// - Status decoding is in `etherdream_schema.hpp` to keep wire parsing separate.
// - The worker loop is a simple thread in this version; you can migrate to an
//   `asio::steady_timer` on the io_context for a fully single-threaded design.


// EtherDreamDevice.hpp

#pragma once
#include "libera/core/Expected.hpp"
#include "libera/core/LaserDeviceBase.hpp"
#include "libera/net/NetConfig.hpp"
#include "libera/net/TcpClient.hpp"
#include "libera/etherdream/EtherDreamConfig.hpp"
#include "libera/etherdream/etherdream_schema.hpp"
#include <memory>
#include <string_view>
#include <optional>

namespace libera::etherdream {

using libera::expected;
namespace ip = libera::net::asio::ip;

/**
 * @brief Streaming controller that talks to an EtherDream DAC.
 *
 * The device inherits latency management, worker thread lifecycle, and point
 * buffering from LaserDeviceBase. The latency budget exposed by the base class
 * is used both for sizing refill batches and for per-operation TCP deadlines.
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
    struct DacAck {
        schema::DacStatus status{};
        char command = 0;
    };

    /// Wait for the "a" response frame to a specific command.
    expected<DacAck>
    waitForResponse(char command);

    /// Send a single-byte command and synchronously wait for its ACK.
    expected<DacAck>
    sendCommand(char command);

    /// Issue the point-rate command ('q') and return the associated ACK.
    expected<DacAck>
    setPointRate(std::uint16_t rate);

    static std::size_t calculateMinimumPoints(const schema::DacStatus& status,
                                              long long maxLatencyMillis);

    static std::size_t clampDesiredPoints(std::size_t minimumPointsNeeded,
                                          std::size_t minPacketPoints,
                                          std::size_t bufferFree);

    static std::chrono::milliseconds computeSleepDuration(const schema::DacStatus& status,
                                                          std::size_t bufferCapacity,
                                                          std::size_t minPacketPoints);

    void handleFailure(std::string_view where,
                       const std::error_code& ec,
                       bool& failureEncountered);

    schema::DacStatus lastKnownStatus{};
    libera::net::TcpClient tcpClient;
    std::optional<libera::net::asio::ip::address> rememberedAddress{};
    bool rateChangePending = false;
};

} // namespace libera::etherdream
