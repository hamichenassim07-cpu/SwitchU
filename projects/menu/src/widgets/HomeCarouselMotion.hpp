#pragma once

#include "HomeCarouselStyle.hpp"
#include "HomeUiTween.hpp"
#include <algorithm>
#include <cmath>

namespace switchu::homeui {

// Shared motion engine for the HOME hero carousel.
// IconGrid (Jeux / Applications) and SwitchU Music both own one instance of
// this state and call the exact same transition, touch, inertia and snap code.
struct HomeCarouselMotionState {
    float position = 0.f;
    float velocity = 0.f;
    float snapTarget = 0.f;
    bool touchScrolling = false;
    bool inertiaActive = false;
    bool snapActive = false;
};

struct HomeCarouselMotionUpdate {
    bool changed = false;
    bool settled = false;
};

inline float carouselMaxPosition(int itemCount) {
    return static_cast<float>(std::max(0, itemCount - 1));
}

inline void clampCarouselMotion(HomeCarouselMotionState& state, int itemCount) {
    const float maximum = carouselMaxPosition(itemCount);
    state.position = std::clamp(state.position, 0.f, maximum);
    state.snapTarget = std::clamp(state.snapTarget, 0.f, maximum);
    if (itemCount <= 0) {
        state = {};
    }
}

inline void jumpCarouselTo(HomeCarouselMotionState& state, int index, int itemCount) {
    const float maximum = carouselMaxPosition(itemCount);
    const float target = std::clamp(static_cast<float>(index), 0.f, maximum);
    state.position = target;
    state.velocity = 0.f;
    state.snapTarget = target;
    state.touchScrolling = false;
    state.inertiaActive = false;
    state.snapActive = false;
}

inline bool retargetCarouselSnap(HomeCarouselMotionState& state,
                                 int index,
                                 int itemCount) {
    const float maximum = carouselMaxPosition(itemCount);
    const float target = std::clamp(static_cast<float>(index), 0.f, maximum);

    // Controller navigation owns the same continuous coordinate as touch.
    state.touchScrolling = false;
    state.inertiaActive = false;
    state.velocity = 0.f;
    state.snapTarget = target;

    const float difference = target - state.position;
    if (std::abs(difference) < 0.0025f) {
        state.position = target;
        state.snapActive = false;
        return true;
    }

    state.snapActive = true;
    return false;
}

inline bool canTouchCarousel(int itemCount) {
    return itemCount > 1;
}

inline void beginCarouselTouch(HomeCarouselMotionState& state, int itemCount) {
    if (!canTouchCarousel(itemCount))
        return;
    state.touchScrolling = true;
    state.inertiaActive = false;
    state.snapActive = false;
    state.velocity = 0.f;
}

inline bool dragCarouselTouch(HomeCarouselMotionState& state,
                              float deltaPixelsX,
                              int itemCount,
                              float step = kCarouselNeighborSize + kCarouselGap) {
    if (!state.touchScrolling || !canTouchCarousel(itemCount))
        return false;

    if (step <= 0.f)
        return false;

    state.position -= deltaPixelsX / step;
    state.position = std::clamp(state.position, 0.f, carouselMaxPosition(itemCount));
    return true;
}

inline void startCarouselSnapToNearest(HomeCarouselMotionState& state,
                                       int itemCount) {
    state.inertiaActive = false;
    state.velocity = 0.f;
    state.snapTarget = std::clamp(std::round(state.position),
                                  0.f,
                                  carouselMaxPosition(itemCount));
    state.snapActive = true;
}

inline void endCarouselTouch(HomeCarouselMotionState& state,
                             float fingerVelocityPixelsPerSecond,
                             int itemCount,
                             float step = kCarouselNeighborSize + kCarouselGap) {
    if (!state.touchScrolling)
        return;

    state.touchScrolling = false;
    if (step <= 0.f) {
        startCarouselSnapToNearest(state, itemCount);
        return;
    }

    const float rawVelocity = -fingerVelocityPixelsPerSecond / step;
    const float rawSpeed = std::abs(rawVelocity);
    const float powerBoost = 1.f + std::clamp((rawSpeed - 1.f) * 0.18f,
                                               0.f,
                                               1.15f);
    // Touch impulses remain below roughly one extra cover with existing drag.
    // No change to controller retargeting, horizontal spacing or snap speed.
    state.velocity = std::clamp(rawVelocity * powerBoost, -3.6f, 3.6f);

    if (std::abs(state.velocity) < kCarouselMinimumInertiaSpeed) {
        state.velocity = 0.f;
        startCarouselSnapToNearest(state, itemCount);
    } else {
        state.inertiaActive = true;
        state.snapActive = false;
    }
}

inline HomeCarouselMotionUpdate updateCarouselMotion(HomeCarouselMotionState& state,
                                                       float dt,
                                                       int itemCount) {
    HomeCarouselMotionUpdate result{};
    if (itemCount <= 0 || state.touchScrolling)
        return result;

    dt = uiDelta(dt);
    const float maximum = carouselMaxPosition(itemCount);

    if (state.inertiaActive) {
        state.position += state.velocity * dt;

        if (state.position <= 0.f) {
            state.position = 0.f;
            if (state.velocity < 0.f) state.velocity = 0.f;
        } else if (state.position >= maximum) {
            state.position = maximum;
            if (state.velocity > 0.f) state.velocity = 0.f;
        }

        state.velocity *= std::exp(-kCarouselInertiaFriction * dt);
        result.changed = true;

        if (std::abs(state.velocity) < kCarouselMinimumInertiaSpeed)
            startCarouselSnapToNearest(state, itemCount);
        return result;
    }

    if (state.snapActive) {
        const float difference = state.snapTarget - state.position;
        const float amount = std::min(1.f, kCarouselSnapSpeed * dt);
        state.position += difference * amount;
        result.changed = true;

        if (std::abs(difference) < 0.0025f) {
            state.position = state.snapTarget;
            state.snapActive = false;
            state.inertiaActive = false;
            state.velocity = 0.f;
            result.settled = true;
        }
    }

    return result;
}

} // namespace switchu::homeui
