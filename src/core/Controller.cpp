#include "Controller.hpp"
#include <cassert>

namespace libera::core {

Controller::Controller() {
    // Pre-reserve a large capacity so vectors can be reused
    // without reallocating in the real-time path.
    // 30,000 points is intentionally generous — safe for most DACs.
    pointsToSend.reserve(30000);
    newPoints.reserve(30000);
}

void Controller::setRequestPointsCallback(const RequestPointsCallback &callback) {
    // Store the callback (copied in).
    requestPointsCallback = callback;
}

bool Controller::pullOnce(const PointFillRequest &request) {
    if (!requestPointsCallback) {
        // No callback set — cannot produce points.
        return false;
    }

    // Reset scratch buffer (capacity retained).
    newPoints.clear();

    // Ask user-supplied callback to append new points.
    requestPointsCallback(request, newPoints);

    // Debug-only: enforce the contract that callback produced
    // at least the requested minimum number of points.
    assert(newPoints.size() >= request.minimumPointsRequired &&
           "Callback did not provide the minimum required number of points.");

    // Append the newly generated batch to the main buffer.
    pointsToSend.insert(pointsToSend.end(), newPoints.begin(), newPoints.end());

    return true;
}


void Controller::start() {
    if (running) return; // already running
    running = true;
    worker = std::thread([this] {
        this->run(); // calls the virtual run(), so subclass overrides work
    });
}

void Controller::stop() {
    if (running) {
        running = false;
        if (worker.joinable()) {
            worker.join();
        }
    }
}



} // namespace libera::core
