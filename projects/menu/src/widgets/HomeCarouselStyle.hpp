#pragma once

#include <algorithm>
#include <cmath>

namespace switchu::homeui {

// Canonical HOME V10.30 carousel geometry and motion.
// Games, Applications and Music all consume these exact values/functions.
inline constexpr int kCarouselVisibleIcons = 5;
inline constexpr float kCarouselSelectedSize = 310.f;
inline constexpr float kCarouselNeighborSize = 230.f;
inline constexpr float kCarouselGap = 12.f;
inline constexpr float kCarouselBaselineY = 512.f;
inline constexpr float kCarouselSnapSpeed = 14.f;
inline constexpr float kCarouselSelectionBounceDuration = 0.42f;
inline constexpr float kCarouselInertiaFriction = 4.4f;
inline constexpr float kCarouselMinimumInertiaSpeed = 0.14f;
inline constexpr float kCarouselMaximumInertiaSpeed = 14.f;

static_assert(kCarouselVisibleIcons >= 3);
static_assert(kCarouselSelectedSize > kCarouselNeighborSize);
static_assert(kCarouselNeighborSize > 0.f);
static_assert(kCarouselGap >= 0.f);
static_assert(kCarouselSnapSpeed > 0.f);
static_assert(kCarouselSelectionBounceDuration > 0.f);
static_assert(kCarouselInertiaFriction > 0.f);
static_assert(kCarouselMinimumInertiaSpeed > 0.f);
static_assert(kCarouselMaximumInertiaSpeed > kCarouselMinimumInertiaSpeed);

inline float carouselIconSizeForDistance(float distance) {
    distance = std::abs(distance);
    if (distance <= 1.f) {
        return kCarouselSelectedSize +
               (kCarouselNeighborSize - kCarouselSelectedSize) * distance;
    }
    return kCarouselNeighborSize;
}

inline float carouselCenterOffset(float distance) {
    const float sign = distance < 0.f ? -1.f : 1.f;
    const float a = std::abs(distance);
    const float firstStep =
        kCarouselSelectedSize * 0.5f +
        kCarouselNeighborSize * 0.5f +
        kCarouselGap;
    const float neighborStep = kCarouselNeighborSize + kCarouselGap;

    const float magnitude =
        (a <= 1.f)
            ? a * firstStep
            : firstStep + (a - 1.f) * neighborStep;
    return sign * magnitude;
}

inline float carouselSelectionBounceScale(float t) {
    if (t <= 0.f || t >= kCarouselSelectionBounceDuration)
        return 1.f;
    return 1.f + 0.080f * std::exp(-7.0f * t) * std::sin(22.0f * t);
}

} // namespace switchu::homeui
