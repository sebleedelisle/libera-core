#pragma once

#include <vector>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include "LaserPoint.hpp"

namespace libera::core {

/**
 * @brief Information provided when the controller asks for new points.
 */
struct PointFillRequest {
    /// Minimum number of points that must be produced by the callback.
    std::size_t minimumPointsRequired = 0;

    /// Host-side estimate of when the first point in this batch will reach the mirrors.
    /// (This is advisory — implementations can ignore or use it for scheduling.)
    std::chrono::steady_clock::time_point estimatedFirstPointRenderTime{};

    std::uint64_t currentPointIndex = 0; // absolute running counter
};

/**
 * @brief Callback contract for point generation.
 *
 * The callback must:
 * - Append new points to @p outputBuffer using push_back or emplace_back.
 * - Produce at least request.minimumPointsRequired points.
 * - Not call reserve() or resize() on @p outputBuffer (no allocations in the RT path).
 * - It may produce more than the minimum, up to outputBuffer.capacity().
 *
 * The caller will read outputBuffer.size() after the callback to know how many points were written.
 */
using RequestPointsCallback =
    std::function<void(const PointFillRequest &request,
                       std::vector<LaserPoint> &outputBuffer)>;

/**
 * @brief Base controller class that manages callback-driven point generation.
 *
 * Subclasses (e.g. EtherDreamController, HeliosController) are responsible
 * for actually sending points to hardware. This base class only handles:
 * - Storing a user-provided callback.
 * - Requesting batches of new points via pullOnce().
 * - Accumulating generated points into an internal buffer for later use.
 */
class Controller {
public:
    /**
     * @brief Construct the controller and reserve internal buffers.
     *
     * Currently reserves ~30k points for both buffers, which is more than most
     * hardware FIFOs. This avoids most reallocations in practice.
     */
    Controller();

    /**
     * @brief Install or replace the callback that generates points.
     * @param callback Function object or lambda conforming to RequestPointsCallback.
     */
    void setRequestPointsCallback(const RequestPointsCallback &callback);

    /**
     * @brief Ask the callback for more points and append them to the main buffer.
     *
     * Typical usage is from a hardware-specific run loop: call pullOnce() to
     * generate points, then send pointsToSend to the DAC.
     *
     * @param request Fill request (min points required, estimated render time).
     * @return false if no callback is installed, true if points were appended.
     */
    bool pullOnce(const PointFillRequest &request);


     /// Start the worker thread.
    void start();

    /// Request the thread to stop and wait for it to finish.
    void stop();

protected:


    virtual void run() = 0; // the worker loop

    std::thread worker;
    std::atomic<bool> running{false};

    /// The installed callback that generates points (may be empty if not set).
    RequestPointsCallback requestPointsCallback{};

    /// Main buffer of points pending transmission to the DAC.
    std::vector<LaserPoint> pointsToSend;

    /// Scratch buffer for a single pullOnce() batch (reused each call).
    std::vector<LaserPoint> newPoints;


};

} // namespace libera::core
