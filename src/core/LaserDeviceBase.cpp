#include "libera/core/LaserDeviceBase.hpp"
#include <cassert>

namespace libera::core {

LaserDeviceBase::LaserDeviceBase() {
    // Pre-reserve a large capacity so vectors can be reused
    // without reallocating in the real-time path.
    // 30,000 points is intentionally generous — safe for most DACs.
    pointsToSend.reserve(30000);
    newPoints.reserve(30000);
}

LaserDeviceBase::~LaserDeviceBase() {
    stop();
}

void LaserDeviceBase::setRequestPointsCallback(const RequestPointsCallback &callback) {
    // Store the callback (copied in).
    requestPointsCallback = callback;
}

bool LaserDeviceBase::requestPoints(const PointFillRequest &request) {
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
    if (request.maximumPointsRequired > 0) {
        assert(newPoints.size() <= request.maximumPointsRequired &&
               "Callback produced more points than allowed by maximumPointsRequired.");
    }

    // Append the newly generated batch to the main buffer.
    // Note: we intentionally keep `newPoints` separate to easily inspect how
    // many were produced in a single requestPoints() call and to avoid allocations.
    pointsToSend.insert(pointsToSend.end(), newPoints.begin(), newPoints.end());

    return true;
}


void LaserDeviceBase::start() {
    if (running) return; // already running
    running = true;
    worker = std::thread([this] {
        this->run(); // calls the virtual run(), so subclass overrides work
    });
}

void LaserDeviceBase::stop() {
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
