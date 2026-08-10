#pragma once

#include <nxui/core/Renderer.hpp>

namespace switchu::homeui {

inline void applyLiquidGlassV102(nxui::Renderer& ren) {
    auto& lg = ren.liquidGlassSettings();

    // HOME V10.3B / Liquid Glass C: stronger, more visible refraction inspired by liquidDX11.
    // The existing OverShifted/LiquidGlass-inspired nxui shader does the real
    // backdrop refraction/blur; these values make the effect obvious while
    // keeping text rendering outside the refracted pass for readability.
    lg.refractionIntensity = 0.46f;
    lg.blurIntensity = 6.20f;
    lg.noiseIntensity = 0.022f;
    lg.glowIntensity = 0.96f;
    lg.saturation = 1.22f;
    lg.opacityMultiplier = 0.58f;
    lg.roughness = 0.018f;
    lg.animSpeed = 0.0f;
    lg.time = 0.0f;
    lg.powerFactor = 8.10f;
    lg.fPower = 1.58f;
    lg.refA = 1.08f;
    lg.refB = 3.36f;
    lg.refC = 6.85f;
    lg.refD = 8.10f;
    lg.glowWeight = 0.42f;
    lg.glowBias = 0.030f;
    lg.glowEdge0 = 0.16f;
    lg.glowEdge1 = -0.18f;
    lg.tintBoost = nxui::Color(1.05f, 1.10f, 1.16f, 1.00f);
}

} // namespace switchu::homeui
