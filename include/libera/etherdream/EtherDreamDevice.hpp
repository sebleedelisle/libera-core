

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
#include "libera/core/LaserDeviceBase.hpp"
#include "libera/net/NetConfig.hpp"
#include "libera/net/TcpClient.hpp"
#include "libera/etherdream/etherdream_schema.hpp"
#include <optional>
#include "tl/expected.hpp"

namespace libera::etherdream {

class EtherDreamDevice : public libera::core::LaserDeviceBase {
public:
    explicit EtherDreamDevice(libera::net::asio::io_context& ioContext);
    ~EtherDreamDevice();

    // non-copyable / non-movable
    EtherDreamDevice(const EtherDreamDevice&) = delete;
    EtherDreamDevice& operator=(const EtherDreamDevice&) = delete;
    EtherDreamDevice(EtherDreamDevice&&) = delete;
    EtherDreamDevice& operator=(EtherDreamDevice&&) = delete;

    tl::expected<void, std::error_code>
    connect(const libera::net::asio::ip::address& address);
    tl::expected<void, std::error_code>
    connect(const std::string& addressstring); 
    void close();                        // idempotent
    bool isConnected() const;           // const-safe

    
protected:
    void run() override;

    tl::expected<schema::DacStatus, std::error_code>
        read_status(std::chrono::milliseconds timeout);


private:
    libera::net::TcpClient tcpClient;
    std::optional<libera::net::asio::ip::address> rememberedAddress{};
};

} // namespace libera::etherdream
