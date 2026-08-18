#include "libera/core/PointStreamFramer.hpp"
#include "libera/core/LaserController.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace libera::core {
namespace {

constexpr std::size_t liberationMinBlankRunPoints = 4;
constexpr float liberationHomeThreshold = 0.03f;
constexpr float liberationTransitionDistanceThreshold = 0.15f;
constexpr float liberationBoundaryScoreThreshold = 0.55f;
constexpr float liberationBlankRunScoreTarget = 64.0f;
constexpr float liberationHomeDwellScoreTarget = 8.0f;

float distanceBetween(const LaserPoint& a, const LaserPoint& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float forgivingSizeScore(std::size_t candidateIndex, std::size_t nominalFrameSize) {
    if (nominalFrameSize == 0) {
        return 0.0f;
    }

    const float sizeRatio =
        static_cast<float>(candidateIndex) / static_cast<float>(nominalFrameSize);
    return 1.0f - std::clamp(std::abs(sizeRatio - 1.0f) * 0.35f, 0.0f, 1.0f);
}

} // namespace

void PointStreamFramer::setNominalFrameSize(std::size_t size) {
    nominalFrameSize = std::max<std::size_t>(size, 1);
}

void PointStreamFramer::setMaxFrameSize(std::size_t size) {
    maxFrameSize = std::max<std::size_t>(size, 1);
}

void PointStreamFramer::setVirtualBufferTarget(std::size_t size) {
    virtualBufferTarget = size;
}

void PointStreamFramer::setTransportBufferedPoints(std::size_t size) {
    transportBufferedPoints = size;
}

std::size_t PointStreamFramer::bufferedPointCount() const {
    return accumulator.size() + preparedPointCount;
}

bool PointStreamFramer::extractFrame(const RequestPointsCallback& callback,
                                     const PointFillRequest& templateRequest,
                                     Frame& outputFrame) {
    if (!callback) {
        return false;
    }

    ensurePreparedFrames(callback, templateRequest, 1 + preparedFrameReserveCount);
    if (preparedFrames.empty()) {
        return false;
    }

    outputFrame = Frame{};
    outputFrame.points = std::move(preparedFrames.front());
    preparedPointCount -= outputFrame.points.size();
    preparedFrames.pop_front();
    return !outputFrame.points.empty();
}

void PointStreamFramer::ensurePreparedFrames(const RequestPointsCallback& callback,
                                             const PointFillRequest& templateRequest,
                                             std::size_t desiredReadyFrames) {
    if (!callback) {
        return;
    }

    while (preparedFrames.size() < desiredReadyFrames) {
        // Determine how many points we want in the accumulator before searching.
        // Pull a modest multiple of nominal so the search has room to find
        // boundaries in frames somewhat larger than nominal, without buffering
        // the full maxFrameSize and inflating latency. The search window itself
        // extends to whatever is already in the accumulator (up to maxFrameSize),
        // so larger frames are found naturally as the accumulator grows across
        // successive extractFrame calls.
        const std::size_t lookaheadTarget =
            (consecutiveForceEmits >= forceEmitFallbackCount)
                ? std::min(static_cast<std::size_t>(nominalFrameSize * 1.1f), maxFrameSize)
                : std::min(nominalFrameSize * 4, maxFrameSize);

        // Two separate pull requirements:
        // 1. The accumulator itself must have enough data for the search window.
        //    Transport-buffered points are already downstream and can't be searched.
        // 2. The total virtual backlog (accumulator + transport) should not exceed
        //    the desired target, to avoid over-generating content.
        const std::size_t accumulatorDeficit =
            (accumulator.size() < lookaheadTarget)
                ? (lookaheadTarget - accumulator.size())
                : 0;
        const std::size_t totalBuffered =
            transportBufferedPoints + preparedPointCount + accumulator.size();
        const std::size_t desiredBufferedPoints =
            std::max(virtualBufferTarget, lookaheadTarget);
        const std::size_t backlogDeficit =
            (totalBuffered < desiredBufferedPoints)
                ? (desiredBufferedPoints - totalBuffered)
                : 0;
        const std::size_t pullCount = std::max(accumulatorDeficit, backlogDeficit);

        if (pullCount > 0) {
            pullPoints(callback, templateRequest, pullCount);
        }

        if (accumulator.empty()) {
            return;
        }

        std::vector<LaserPoint> preparedFrame;
        if (!extractOneFrame(preparedFrame)) {
            return;
        }
        preparedPointCount += preparedFrame.size();
        preparedFrames.push_back(std::move(preparedFrame));
    }
}

bool PointStreamFramer::extractOneFrame(std::vector<LaserPoint>& framePoints) {
    if (accumulator.empty()) {
        return false;
    }

    // Establish anchor from the first point if needed.
    if (!anchorSet) {
        anchorX = accumulator[0].x;
        anchorY = accumulator[0].y;
        anchorSet = true;
    }

    // Search for a natural boundary.
    const std::size_t splitIndex = findNaturalBoundary();

    if (splitIndex > 0) {
        // Natural boundary found — emit points [0, splitIndex).
        framePoints.assign(accumulator.begin(),
                           accumulator.begin() + static_cast<std::ptrdiff_t>(splitIndex));
        accumulator.erase(accumulator.begin(),
                          accumulator.begin() + static_cast<std::ptrdiff_t>(splitIndex));

        // Reset anchor to the start of the remaining accumulator.
        if (!accumulator.empty()) {
            anchorX = accumulator[0].x;
            anchorY = accumulator[0].y;
        } else {
            anchorSet = false;
        }
        consecutiveForceEmits = 0;
        return !framePoints.empty();
    }

    // No natural boundary — force-emit at nominal size.
    const std::size_t emitCount = std::min(nominalFrameSize, accumulator.size());
    framePoints.assign(accumulator.begin(),
                       accumulator.begin() + static_cast<std::ptrdiff_t>(emitCount));
    accumulator.erase(accumulator.begin(),
                      accumulator.begin() + static_cast<std::ptrdiff_t>(emitCount));

    // Re-establish anchor from whatever is next.
    if (!accumulator.empty()) {
        anchorX = accumulator[0].x;
        anchorY = accumulator[0].y;
    } else {
        anchorSet = false;
    }
    ++consecutiveForceEmits;
    return !framePoints.empty();
}

void PointStreamFramer::reset() {
    accumulator.clear();
    preparedFrames.clear();
    preparedPointCount = 0;
    anchorX = 0.0f;
    anchorY = 0.0f;
    anchorSet = false;
    virtualBufferTarget = 0;
    transportBufferedPoints = 0;
    consecutiveForceEmits = 0;
    totalPointsConsumed = 0;
}

void PointStreamFramer::pullPoints(const RequestPointsCallback& callback,
                                   const PointFillRequest& templateRequest,
                                   std::size_t count) {
    if (count == 0) return;

    PointFillRequest req = templateRequest;
    req.minimumPointsRequired = count;
    req.maximumPointsRequired = count;
    req.allowShortRead = true;
    // Keep the callback's absolute point index advancing from the transport's
    // current playout cursor rather than restarting at zero inside the framer.
    req.currentPointIndex = templateRequest.currentPointIndex + totalPointsConsumed;

    std::vector<LaserPoint> batch;
    batch.reserve(count);
    callback(req, batch);
    sanitizeLaserPoints(batch);

    accumulator.insert(accumulator.end(), batch.begin(), batch.end());
    totalPointsConsumed += batch.size();
}

std::size_t PointStreamFramer::findNaturalBoundary() const {
    if (accumulator.size() < 2) {
        return 0;
    }

    const float minFactor = (consecutiveForceEmits >= forceEmitFallbackCount)
                                ? 0.9f
                                : searchWindowMinFactor;

    const std::size_t windowStart =
        static_cast<std::size_t>(nominalFrameSize * minFactor);
    const std::size_t windowEnd =
        std::min(maxFrameSize, accumulator.size());

    if (windowStart >= windowEnd) {
        return 0;
    }

    std::size_t bestLoopCandidate = 0;
    float bestLoopScore = -1.0f;

    for (std::size_t i = windowStart; i < windowEnd; ++i) {
        const LaserPoint& p = accumulator[i];

        if (!isBlanked(p)) {
            continue;
        }

        // Distance from anchor.
        const float dx = p.x - anchorX;
        const float dy = p.y - anchorY;
        const float dist = std::sqrt(dx * dx + dy * dy);

        // Position score: closer to anchor is better.
        const float positionScore = 1.0f - std::clamp(dist / distanceThreshold, 0.0f, 1.0f);
        if (positionScore <= 0.0f) {
            continue; // Too far from anchor.
        }

        // Size score: prefer frame sizes near 1.0x nominal.
        const float sizeRatio = static_cast<float>(i) / static_cast<float>(nominalFrameSize);
        const float sizeScore = 1.0f - std::abs(sizeRatio - 1.0f);

        // Bonus: prefer the last blanked point before lit content resumes
        // (the blank-to-lit transition is the ideal split point).
        float gapEndBonus = 0.0f;
        if (i + 1 < accumulator.size() && !isBlanked(accumulator[i + 1])) {
            gapEndBonus = 1.0f;
        }

        const float score = positionScore * 0.6f + sizeScore * 0.3f + gapEndBonus * 0.1f;

        if (score > bestLoopScore) {
            bestLoopScore = score;
            bestLoopCandidate = i + 1; // Split AFTER this blanked point.
        }
    }

    // Liberation/ofxLaser-style source frames are not always spatially closed
    // against the first accumulator point. When a new frame starts elsewhere,
    // the source frame is usually:
    //   blank move from previous home -> current first lit point,
    //   lit content,
    //   blank return to current first lit point,
    //   blank move toward the next frame's first lit point.
    //
    // The best split for frame-ingester DACs is near the current first lit point
    // before the outgoing blank travel, so the next emitted frame starts dark.
    std::size_t firstLitIndex = 0;
    bool firstLitSet = false;
    for (std::size_t i = 0; i < windowEnd; ++i) {
        if (!isBlanked(accumulator[i])) {
            firstLitIndex = i;
            firstLitSet = true;
            break;
        }
    }

    std::size_t bestTransitionCandidate = 0;
    float bestTransitionScore = -1.0f;

    if (firstLitSet) {
        const LaserPoint& frameHome = accumulator[firstLitIndex];

        for (std::size_t runStart = 0; runStart < windowEnd;) {
            if (!isBlanked(accumulator[runStart])) {
                ++runStart;
                continue;
            }

            std::size_t runEnd = runStart + 1;
            while (runEnd < accumulator.size() && isBlanked(accumulator[runEnd])) {
                ++runEnd;
            }

            const std::size_t runLength = runEnd - runStart;
            const bool overlapsSearchWindow =
                runEnd > windowStart && runStart < windowEnd;
            const bool hasFollowingLit =
                runEnd < accumulator.size() && !isBlanked(accumulator[runEnd]);

            if (overlapsSearchWindow &&
                hasFollowingLit &&
                runLength >= liberationMinBlankRunPoints) {
                std::size_t closestHomeIndex = runStart;
                float closestHomeDistance = std::numeric_limits<float>::max();

                for (std::size_t i = runStart; i < runEnd; ++i) {
                    const float distanceToHome = distanceBetween(accumulator[i], frameHome);
                    if (distanceToHome <= closestHomeDistance) {
                        closestHomeDistance = distanceToHome;
                        closestHomeIndex = i;
                    }
                }

                if (closestHomeDistance <= liberationHomeThreshold) {
                    std::size_t firstNearHomeIndex = closestHomeIndex;
                    while (firstNearHomeIndex > runStart &&
                           distanceBetween(accumulator[firstNearHomeIndex - 1], frameHome) <=
                               liberationHomeThreshold) {
                        --firstNearHomeIndex;
                    }

                    std::size_t lastNearHomeIndex = closestHomeIndex;
                    while (lastNearHomeIndex + 1 < runEnd &&
                           distanceBetween(accumulator[lastNearHomeIndex + 1], frameHome) <=
                               liberationHomeThreshold) {
                        ++lastNearHomeIndex;
                    }

                    const std::size_t splitIndex = lastNearHomeIndex;
                    const std::size_t nearHomePointCount =
                        (lastNearHomeIndex - firstNearHomeIndex) + 1;
                    const bool returnedToHome = firstNearHomeIndex > runStart;
                    const float runTailDistance =
                        distanceBetween(accumulator[runEnd - 1], frameHome);
                    const float nextLitDistance =
                        distanceBetween(accumulator[runEnd], frameHome);
                    const float departureDistance = std::max(runTailDistance, nextLitDistance);

                    if (splitIndex >= windowStart &&
                        splitIndex < windowEnd &&
                        splitIndex > 0 &&
                        lastNearHomeIndex + 1 < runEnd &&
                        departureDistance >= liberationTransitionDistanceThreshold) {
                        const float homeScore =
                            1.0f - std::clamp(closestHomeDistance / liberationHomeThreshold,
                                               0.0f,
                                               1.0f);
                        const float travelScore =
                            std::clamp(departureDistance /
                                           liberationTransitionDistanceThreshold,
                                       0.0f,
                                       1.0f);
                        const float sizeScore = forgivingSizeScore(splitIndex, nominalFrameSize);
                        const float runLengthScore =
                            std::clamp(static_cast<float>(runLength) /
                                           liberationBlankRunScoreTarget,
                                       0.0f,
                                       1.0f);
                        const float homeDwellScore =
                            std::clamp(static_cast<float>(nearHomePointCount) /
                                           liberationHomeDwellScoreTarget,
                                       0.0f,
                                       1.0f);
                        const float returnScore = returnedToHome ? 1.0f : 0.0f;

                        // Internal closed shapes can start their blank move at
                        // frameHome. A real renderer frame end is stronger when
                        // the blank run returns to frameHome and lingers there.
                        const float score = homeScore * 0.25f +
                                            travelScore * 0.20f +
                                            sizeScore * 0.20f +
                                            runLengthScore * 0.10f +
                                            homeDwellScore * 0.15f +
                                            returnScore * 0.10f;

                        if (score >= liberationBoundaryScoreThreshold &&
                            score > bestTransitionScore) {
                            bestTransitionScore = score;
                            bestTransitionCandidate =
                                splitIndex; // Split BEFORE outgoing blank travel.
                        }
                    }
                }
            }

            runStart = runEnd;
        }
    }

    const bool hasLoopBoundary = bestLoopCandidate > 0 && bestLoopScore >= 0.3f;
    const bool hasTransitionBoundary =
        bestTransitionCandidate > 0 &&
        bestTransitionScore >= liberationBoundaryScoreThreshold;

    if (hasLoopBoundary && hasTransitionBoundary) {
        // If a solid loop closure appears before the renderer-transition
        // candidate, keep the closed-frame split. This prevents multi-shape
        // frames from being cut at a later internal return-to-first-shape move.
        if (bestLoopCandidate < bestTransitionCandidate) {
            return bestLoopCandidate;
        }

        // If the transition comes first, the later loop closure is usually the
        // next frame's leading move returning to the old anchor.
        if (bestTransitionCandidate < bestLoopCandidate &&
            bestTransitionScore >= bestLoopScore - 0.15f) {
            return bestTransitionCandidate;
        }

        return bestLoopCandidate;
    }

    if (hasTransitionBoundary) {
        return bestTransitionCandidate;
    }

    if (hasLoopBoundary) {
        return bestLoopCandidate;
    }

    return 0;
}

bool PointStreamFramer::isBlanked(const LaserPoint& p) {
    return (p.r + p.g + p.b) < blankThreshold;
}

} // namespace libera::core
