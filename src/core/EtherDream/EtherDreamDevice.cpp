// EtherDreamDevice.cpp
#include "libera/core/etherdream/EtherDreamDevice.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;

namespace libera::core::etherdream {

EtherDreamDevice::EtherDreamDevice(libera::net::asio::io_context& ioContext)
: tcpClient(ioContext) {}

EtherDreamDevice::~EtherDreamDevice() {
    stop();   // join worker
    close();  // ensure socket is closed
}

bool EtherDreamDevice::connect(const libera::net::asio::ip::address& address) {

    constexpr unsigned short port = 7765;
    libera::net::tcp::endpoint endpoint(address, port);

    libera::net::error_code ec = tcpClient.connect(std::array{endpoint}, 1s);
    if (ec) {
        std::cerr << "[EtherDreamDevice] connect failed: " << ec.message()
                  << " (to " << address.to_string() << ":" << port << ")\n";
        return false;
    }

    tcpClient.setLowLatency();

    rememberedAddress = address;

    std::cout << "[EtherDreamDevice] connected to "
              << address.to_string() << ":" << port << "\n";

    return true;
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

} // namespace libera::core::etherdream
