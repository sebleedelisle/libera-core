#include "libera/net/NetService.hpp"
#include "libera/etherdream/EtherDreamDevice.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace libera;

int main() {

    // creates the net service for the whole app
    net::NetService net;

    etherdream::EtherDreamDevice etherdream(net.io());

    // 3) Install your point-generation callback.
    etherdream.setRequestPointsCallback(
        [](const core::PointFillRequest& req, std::vector<core::LaserPoint>& out) {
            out.reserve(out.size() + req.minimumPointsRequired);
            for (size_t i = 0; i < req.minimumPointsRequired; ++i) {
                // Dummy point: zero coords, full RGB+intensity, flags=0
                out.push_back(core::LaserPoint{0, 0, 1, 1, 1, 1, 0});
            }
        }
    );

    // 4) (Optional) Connect to a real EtherDream on your LAN.
    //    If you’re just testing the threading/callback flow, you can skip this.
    //    Replace the IP below with your device address when ready.
    
    libera::net::error_code ec;
    auto ip = libera::net::asio::ip::make_address("192.168.1.76", ec);
    if (ec) {
        std::cerr << "Invalid IP: " << ec.message() << "\n";
    } else if (!etherdream.connect(ip)) {
        std::cerr << "Connect failed\n";
    } else { 
                // 5) Start the device worker thread (calls EtherDreamDevice::run()).
        std::cout << "Starting dummy run..." << std::endl;
        etherdream.start();

        // Keep main alive long enough for the worker to do a few ticks.
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // 6) Stop the device worker and close the socket (if you connected).
        etherdream.stop();
        etherdream.close();
        std::cout << "Done." << std::endl;
    }
    

    return 0;
}
