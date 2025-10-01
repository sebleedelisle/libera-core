#include "libera/net/NetService.hpp"
#include "libera/etherdream/EtherDreamDevice.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

using namespace libera;

int main() {

    // creates the net service for the whole app
    net::NetService net;

    etherdream::EtherDreamDevice etherdream(net.io());

    // 3) Install your point-generation callback.
    //    This demonstrates the LaserDeviceBase contract: append N points to
    //    the provided vector without allocating (no reserve/resize here).
    etherdream.setRequestPointsCallback(
        [](const core::PointFillRequest& req, std::vector<core::LaserPoint>& out) {
            const std::size_t pointCount = req.minimumPointsRequired;
            if (pointCount == 0) {
                return;
            }

            // Evenly distribute points along the unit circle.
            const float tau = 2.0f * static_cast<float>(std::acos(-1.0));
            for (std::size_t i = 0; i < pointCount; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(pointCount);
                const float angle = t * tau;
                const float x = std::cos(angle);
                const float y = std::sin(angle);

                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;

                if (x >= 0.0f && y >= 0.0f) {
                    r = g = b = 1.0f; // quadrant I – white
                } else if (x < 0.0f && y >= 0.0f) {
                    r = 1.0f;         // quadrant II – red
                } else if (x < 0.0f && y < 0.0f) {
                    g = 1.0f;         // quadrant III – green
                } else {
                    b = 1.0f;         // quadrant IV – blue
                }

                out.push_back(core::LaserPoint{x, y, r, g, b, 1.0f, 0.0f, 0.0f});
            }
        }
    );

    // 4) (Optional) Connect to a real EtherDream on your LAN.
    //    If you’re just testing the threading/callback flow, you can skip this.
    //    Replace the IP below with your device address when ready.
    //    On macOS you may need to allow the app in firewall prompts.
    
   if (auto r = etherdream.connect("127.0.0.1"); !r) {
        std::cerr << "Connect failed: " << r.error().message() << "\n";
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
