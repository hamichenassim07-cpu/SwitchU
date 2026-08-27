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
float vinylCenterShiftRadiusUnits(float reveal) { return 1.32f*reveal; }
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

    // V8.6: the root intentionally hides the record, while detailed views
    // push the vinyl centre far enough out from behind the sleeve to keep its
    // silhouette readable on the real Switch display.
    assert(std::abs(vinylCenterShiftRadiusUnits(0.0f)-0.0f)<0.001f);
    assert(std::abs(vinylCenterShiftRadiusUnits(0.72f)-0.9504f)<0.001f);
    assert(std::abs(vinylCenterShiftRadiusUnits(0.98f)-1.2936f)<0.001f);

    // V8.6 contract: category exchange stays 200-250 ms, the physical detail
    // hand-off remains <=500 ms, Music -> HOME stays <=160 ms, and the
    // Now Playing hand-off gets a little more time so sleeve/vinyl/text can
    // travel rather than pop.
    using namespace switchu::menu::music::timing;
    assert(kRootCategoryTransitionSeconds >= 0.20f && kRootCategoryTransitionSeconds <= 0.25f);
    assert(kDetailTransformSeconds <= 0.50f);
    assert(kExitToHomeSeconds <= 0.16f);
    assert(kNowPlayingEnterSeconds >= 0.25f && kNowPlayingEnterSeconds <= 0.32f);
    assert(kNowPlayingExitSeconds >= 0.20f && kNowPlayingExitSeconds <= 0.26f);
    return 0;
}
