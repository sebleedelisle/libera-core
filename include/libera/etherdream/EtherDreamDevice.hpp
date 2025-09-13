

// Ether Dream controller. 
// To figure out! 
// 1.   Connect to DAC
//      Requires networking code for opening and connecting to a 
//      TCP socket. Along with all the error checking that goes with
//      that. 
// 2.   Sending points.
//      Convert from laser points to ether dream points.
//      Create command packets
// 3.   Main thread logic
//      When to ask for more points. When to send them 
// 4.   Receiving ACKs and parse 
// 5.   Status information
//      What is universal status info vs what is ether dream specific? 
// TODO 
// Start with a minimal example
// Connects to network. 
// Starts thread
// Stops on stop command


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
