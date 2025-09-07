#include "Dummy/DummyController.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace libera::core;
using namespace libera::core::dummy;

int main() {
    DummyController ctl;

    ctl.setRequestPointsCallback(
        [](const PointFillRequest& req, std::vector<LaserPoint>& out) {
            for (size_t i = 0; i < req.minimumPointsRequired; ++i) {
                out.push_back(LaserPoint{0,0,1,1,1,1,0});
            }
        });

    std::cout << "Starting dummy run..." << std::endl;
    ctl.start(); // launches the worker thread

    // Keep the main thread alive long enough for the worker to do its job
    std::this_thread::sleep_for(std::chrono::seconds(3));

    ctl.stop(); // signal the worker to exit and join
    std::cout << "Done." << std::endl;
    return 0;
}
