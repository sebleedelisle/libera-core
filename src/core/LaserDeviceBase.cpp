#include "libera/core/LaserDeviceBase.hpp"
#include "libera/log/Log.hpp"
#include <cassert>

namespace libera::core {

LaserDeviceBase::LaserDeviceBase() {
    // Pre-reserve a large capacity so vectors can be reused without reallocating.
    // Thirty thousand points is intentionally generous and remains safe for most DACs.
    pointsToSend.reserve(30000);
}

LaserDeviceBase::~LaserDeviceBase() {
    stop();
}

void LaserDeviceBase::setRequestPointsCallback(const RequestPointsCallback &callback) {
    // Store the callback (copied into the functor).
    requestPointsCallback = callback;
}

bool LaserDeviceBase::requestPoints(const PointFillRequest &request) {
    if (!requestPointsCallback) {
        // No callback set, so there is no way to produce points.
        return false;
    }

    // Reset transmission buffer while retaining capacity.
    pointsToSend.clear();

    // Ask the user-supplied callback to append new points.
    requestPointsCallback(request, pointsToSend);

    // Debug-only: enforce the contract that the callback produced at least the requested minimum.
    assert(pointsToSend.size() >= request.minimumPointsRequired &&
           "Callback did not provide the minimum required number of points.");
    if (request.maximumPointsRequired > 0) {
        assert(pointsToSend.size() <= request.maximumPointsRequired &&
               "Callback produced more points than allowed by maximumPointsRequired.");
    }

    return true;
}


void LaserDeviceBase::start() {
    if (running) return; // Already running.
    running = true;
    worker = std::thread([this] {
        this->run(); // Calls the virtual run(), so subclass overrides execute.
    });
}

void LaserDeviceBase::stop() {
    // Signal the worker loop to exit and wait until the background thread finishes.
    logInfo("[EtherDreamDevice] stop()\n");
    running = false;
    if (worker.joinable()) {
        worker.join();
    }
}


void LaserDeviceBase::setLatency(long long latencyMillisValue) {
    if (latencyMillisValue < 1) {
        latencyMillisValue = 1;
    }
    latencyMillis.store(latencyMillisValue, std::memory_order_relaxed);
}

long long LaserDeviceBase::getLatency() const {
    return latencyMillis.load(std::memory_order_relaxed);
}



} // namespace libera::core
