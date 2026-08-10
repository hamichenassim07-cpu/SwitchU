#pragma once

#include <nxui/core/Renderer.hpp>

namespace switchu::homeui {

inline void applyLiquidGlassV102(nxui::Renderer& ren) {
    auto& lg = ren.liquidGlassSettings();

    // HOME V10.3 / Liquid Glass C: deliberately assertive refraction.
    // The existing OverShifted/LiquidGlass-inspired nxui shader does the real
    // backdrop refraction/blur; these values make the effect obvious while
    // keeping text rendering outside the refracted pass for readability.
    lg.refractionIntensity = 0.285f;
    lg.blurIntensity = 4.65f;
    lg.noiseIntensity = 0.018f;
    lg.glowIntensity = 0.72f;
    lg.saturation = 1.14f;
    lg.opacityMultiplier = 0.41f;
    lg.roughness = 0.010f;
    lg.animSpeed = 0.0f;
    lg.time = 0.0f;
    lg.powerFactor = 6.55f;
    lg.fPower = 1.34f;
    lg.refA = 0.88f;
    lg.refB = 2.88f;
    lg.refC = 5.85f;
    lg.refD = 7.25f;
    lg.glowWeight = 0.34f;
    lg.glowBias = 0.024f;
    lg.glowEdge0 = 0.22f;
    lg.glowEdge1 = -0.10f;
    lg.tintBoost = nxui::Color(1.02f, 1.05f, 1.10f, 0.96f);
}

} // namespace switchu::homeui
