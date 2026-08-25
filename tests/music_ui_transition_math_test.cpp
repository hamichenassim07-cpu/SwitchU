#include <cassert>
#include <cmath>

#include "projects/menu/src/music/MusicUiTiming.hpp"

namespace {
float smoothStep(float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    return v*v*(3.f-2.f*v);
}
float incomingOffset(int direction, float t) {
    const float e=smoothStep(t);
    return -static_cast<float>(direction)*(1.f-e)*52.f;
}
float outgoingOffset(int direction, float t) {
    const float e=smoothStep(t);
    return static_cast<float>(direction)*e*52.f;
}
float exposedDiscFraction(float reveal) { return 0.30f*reveal; }
}

int main() {
    // D-Pad Down: old content exits down, new content arrives from above.
    assert(std::abs(outgoingOffset(+1,0.f)-0.f)<0.001f);
    assert(outgoingOffset(+1,1.f)>51.9f);
    assert(incomingOffset(+1,0.f)<-51.9f);
    assert(std::abs(incomingOffset(+1,1.f))<0.001f);

    // D-Pad Up is the exact inverse.
    assert(outgoingOffset(-1,1.f)<-51.9f);
    assert(incomingOffset(-1,0.f)>51.9f);

    // Root selected album ≈18.6% visible record; detail ≈27.6%.
    assert(std::abs(exposedDiscFraction(0.62f)-0.186f)<0.001f);
    assert(std::abs(exposedDiscFraction(0.92f)-0.276f)<0.001f);

    // Current contract: category exchange stays 200-250 ms, the cinematic
    // detail transform never exceeds 500 ms, and Music -> HOME stays <=160 ms.
    using namespace switchu::menu::music::timing;
    assert(kRootCategoryTransitionSeconds >= 0.20f && kRootCategoryTransitionSeconds <= 0.25f);
    assert(kDetailTransformSeconds <= 0.50f);
    assert(kExitToHomeSeconds <= 0.16f);
    assert(kNowPlayingEnterSeconds <= 0.25f);
    assert(kNowPlayingExitSeconds <= 0.20f);
    return 0;
}
