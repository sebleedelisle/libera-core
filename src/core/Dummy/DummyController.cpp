#include "libera/core/Dummy/DummyController.hpp"
#include <chrono>
#include <iostream>

namespace libera::core::dummy {

DummyController::DummyController() = default;

DummyController::~DummyController() {
    stop(); // ensure thread is joined before destruction
}

void DummyController::run() {
    using namespace std::chrono;

    const auto interval = milliseconds(33); // ~30Hz

    // A minimal example loop that exercises the callback and buffer flow.
    // Real controllers (e.g. EtherDream) usually poll device status and send
    // points as well.
    while (running) {
        PointFillRequest req;
        req.minimumPointsRequired = 1000;
        req.estimatedFirstPointRenderTime = steady_clock::now();

        if (requestPoints(req)) {
            std::cout << "Pulled " << pointsToSend.size()
                      << " new points." << std::endl;
        }

        std::this_thread::sleep_for(interval);
    }
}

} // namespace libera::core::dummy
