// Exact V8.8 geometry, extracted from the delivered archive.
// ZIP SHA-256: 47c81611acda10cf852b325677eeba556fca3d2f0db4990e9faad00c87b40d3a
#pragma once
#include <algorithm>
#include <cmath>
namespace v88 {
constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kBottomY = 681.f;
constexpr float kFloorY = 494.f;
constexpr float kFloorH = kScreenH - kFloorY;
// V8.7 keeps HOME's motion state/inertia/snap, while Music owns a dedicated
// Cover-Flow presentation.  The previous V8.4 reused HOME card geometry, which
// made the screen read as "HOME with covers" instead of the supplied concept.
constexpr float kMusicCarouselBaselineY = 515.f;
// V8.7 is tuned from the real V8.6 Switch capture, not only from nominal pixel
// values.  The selected sleeve stays dominant while neighbours get more air so
// their perspective never reads as two cards intersecting during a slide.
constexpr float kMusicCarouselSelectedSize = 366.f;
constexpr float kMusicCarouselNeighborSize = 294.f;
constexpr float kMusicCarouselFarSize = 274.f;
constexpr float kMusicCarouselFirstOffset = 322.f;
constexpr float kMusicCarouselNeighborStep = 238.f;

float musicCarouselSizeForDistance(float distance) {
    const float a = std::abs(distance);
    if (a <= 1.f) {
        const float t = a * a * (3.f - 2.f * a);
        return kMusicCarouselSelectedSize +
               (kMusicCarouselNeighborSize - kMusicCarouselSelectedSize) * t;
    }
    return std::max(kMusicCarouselFarSize,
                    kMusicCarouselNeighborSize - (a - 1.f) * 14.f);
}

float musicCarouselCenterOffset(float distance) {
    const float sign = distance < 0.f ? -1.f : 1.f;
    const float a = std::abs(distance);
    const float magnitude = a <= 1.f
        ? a * kMusicCarouselFirstOffset
        : kMusicCarouselFirstOffset + (a - 1.f) * kMusicCarouselNeighborStep;
    return sign * magnitude;
}

float musicCarouselYaw(float distance) {
    // V8.5's ~48° extremes were visually dramatic but made the outside covers
    // feel thin/fragile on real hardware.  Keep the inward-facing Cover Flow
    // read while preserving enough front face to recognise every album.
    return std::clamp(-distance * 34.f, -42.f, 42.f);
}

float musicCarouselSideLift(float distance) {
    return std::min(1.f, std::abs(distance)) * 5.f;
}

}
