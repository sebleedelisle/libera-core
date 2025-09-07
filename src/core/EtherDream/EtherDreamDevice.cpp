#include "libera/core/etherdream/EtherDreamDevice.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

namespace libera::core::etherdream {


 EtherDreamDevice::~EtherDreamDevice() {
    stop();
    close();
}

bool EtherDreamDevice::connect(const libera::net::asio::ip::address& address)
{
    constexpr unsigned short port = 7765;

    libera::net::tcp::endpoint endpoint(address, port);

    // 1s timeout connect
    libera::net::error_code ec = tcpClient.connect(std::array{endpoint}, 1s);
    if (ec) {
        std::cerr << "[EtherDreamDevice] connect failed: " << ec.message()
                  << " (to " << address.to_string() << ":" << port << ")\n";
        return false;
    }

    tcpClient.set_low_latency();

    rememberedAddress = address;
    std::cout << "[EtherDreamDevice] connected to "
              << address.to_string() << ":" << port << "\n";
    return true;
}

void EtherDreamDevice::run()
{
    constexpr auto tick = 33ms;
    constexpr std::size_t minPointsPerTick = 1000;

    while (running.load(std::memory_order_relaxed)) {
        PointFillRequest req;
        req.minimumPointsRequired = minPointsPerTick;
        req.estimatedFirstPointRenderTime = std::chrono::steady_clock::now() + tick;

        const bool gotPoints = pullOnce(req);
         
        if (gotPoints) {
            std::cout << "Pulled " << newPoints.size()
                      << " new points. Total buffered: "
                      << pointsToSend.size() << std::endl;
        }

        if (gotPoints && is_connected()) {
            // TODO: serialize & send via tcpClient.write_all(...)
            pointsToSend.clear();
        }

        std::this_thread::sleep_for(tick);
    }
}

bool EtherDreamDevice::is_connected() const {
    // TcpClient::socket() is non-const, but we only need is_open() here.
    return const_cast<EtherDreamDevice*>(this)
                ->tcpClient
                .socket()
                .is_open();
}

/// Convenience accessor so the run loop can implement reconnect logic later.
std::optional<libera::net::asio::ip::address> EtherDreamDevice::lastAddress() const { 
    return rememberedAddress; 
}

 void EtherDreamDevice::close() { 
    //std::cerr << "[EtherDreamDevice] close()\n";
    tcpClient.close(); 
}



} // namespace libera::core::etherdream
