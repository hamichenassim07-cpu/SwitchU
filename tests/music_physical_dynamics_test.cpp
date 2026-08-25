#include <cassert>
#include <cmath>
#include <algorithm>

namespace {
float spinBoostAfter(float initial, float seconds) {
    return initial * std::exp(-seconds * 3.15f);
}
float spinSpeed(float boost) { return 0.74f + 4.10f * boost; }
float parallaxTarget(float motionSignal) {
    return std::clamp(-motionSignal * 0.48f, -3.2f, 3.2f);
}
float reflectedY(float sourceY, float planeY) {
    constexpr float compression = 0.22f;
    return planeY + std::max(0.f, planeY - sourceY) * compression;
}
int vinylSegments(int detailLevel) {
    return detailLevel >= 2 ? 40 : (detailLevel == 1 ? 28 : 20);
}

float litEdge(float dotValue, float ambient) {
    return std::clamp(ambient + (1.f - ambient) * std::max(0.f, dotValue), ambient, 1.f);
}
}

int main() {
    // A track start is visibly faster, but rapidly settles to the calm speed.
    assert(spinSpeed(1.f) > 4.8f);
    assert(spinBoostAfter(1.f, 1.f) < 0.05f);
    assert(std::abs(spinSpeed(0.f) - 0.74f) < 0.001f);

    // Micro-parallax can never exceed the few-pixel contract.
    assert(std::abs(parallaxTarget(100.f) + 3.2f) < 0.001f);
    assert(std::abs(parallaxTarget(-100.f) - 3.2f) < 0.001f);

    // Perspective vinyl reflection is always below the floor plane and compressed.
    const float plane = 510.f;
    assert(reflectedY(410.f, plane) > plane);
    assert(reflectedY(410.f, plane) < 535.f);
    assert(std::abs(reflectedY(530.f, plane) - plane) < 0.001f);

    // Neighbours are cheaper than the selected/detail object.
    assert(vinylSegments(0) < vinylSegments(1));
    assert(vinylSegments(1) < vinylSegments(2));

    // Studio lighting must react to orientation, while retaining a safe
    // ambient floor so a sleeve edge never collapses to black.
    assert(std::abs(litEdge(-1.f, 0.32f) - 0.32f) < 0.001f);
    assert(litEdge(0.65f, 0.32f) > litEdge(0.10f, 0.32f));
    assert(litEdge(1.f, 0.32f) <= 1.0f);
    return 0;
}
