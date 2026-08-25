#include "projects/menu/src/widgets/HomeCarouselMotion.hpp"
#include "projects/menu/src/widgets/HomeTypographyStyle.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
bool near(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    using namespace switchu::homeui;

    static_assert(kCarouselSelectedSize == 310.f);
    static_assert(kCarouselNeighborSize == 230.f);
    static_assert(kCarouselGap == 12.f);
    static_assert(kMinimumMainTextScale == 0.83f);
    static_assert(kMainActionGlyphScale >= kMinimumMainTextScale);

    HomeCarouselMotionState state{};
    jumpCarouselTo(state, 0, 5);
    assert(near(state.position, 0.f));
    assert(!state.snapActive && !state.inertiaActive && !state.touchScrolling);

    // Controller navigation uses the same continuous coordinate and settles
    // exactly on the requested item.
    assert(!retargetCarouselSnap(state, 3, 5));
    for (int i = 0; i < 240 && state.snapActive; ++i)
        updateCarouselMotion(state, 1.f / 60.f, 5);
    assert(!state.snapActive);
    assert(near(state.position, 3.f, 0.0026f));

    // Touch drag follows the canonical neighbour step and remains bounded.
    beginCarouselTouch(state, 5);
    assert(state.touchScrolling);
    const float oneStep = kCarouselNeighborSize + kCarouselGap;
    assert(dragCarouselTouch(state, oneStep, 5));
    assert(near(state.position, 2.f, 0.002f));

    // A fast release enters inertia, then must settle on an integer item.
    endCarouselTouch(state, -1600.f, 5);
    assert(state.inertiaActive || state.snapActive);
    for (int i = 0; i < 720 && (state.inertiaActive || state.snapActive); ++i)
        updateCarouselMotion(state, 1.f / 120.f, 5);
    assert(!state.inertiaActive && !state.snapActive);
    assert(state.position >= 0.f && state.position <= 4.f);
    assert(near(state.position, std::round(state.position), 0.003f));

    // Limits must never overscroll even under extreme finger velocity.
    jumpCarouselTo(state, 0, 5);
    beginCarouselTouch(state, 5);
    dragCarouselTouch(state, 100000.f, 5);
    assert(near(state.position, 0.f));
    endCarouselTouch(state, 100000.f, 5);
    for (int i = 0; i < 240; ++i)
        updateCarouselMotion(state, 1.f / 60.f, 5);
    assert(state.position >= 0.f && state.position <= 4.f);

    jumpCarouselTo(state, 4, 5);
    beginCarouselTouch(state, 5);
    dragCarouselTouch(state, -100000.f, 5);
    assert(near(state.position, 4.f));
    endCarouselTouch(state, -100000.f, 5);
    for (int i = 0; i < 240; ++i)
        updateCarouselMotion(state, 1.f / 60.f, 5);
    assert(state.position >= 0.f && state.position <= 4.f);

    // Empty content resets every bit of transient motion state.
    state.position = 4.f;
    state.velocity = 3.f;
    state.snapActive = true;
    state.inertiaActive = true;
    state.touchScrolling = true;
    clampCarouselMotion(state, 0);
    assert(near(state.position, 0.f));
    assert(near(state.velocity, 0.f));
    assert(!state.snapActive && !state.inertiaActive && !state.touchScrolling);

    std::cout << "HOME carousel/typography contract: OK\n";
    return 0;
}
