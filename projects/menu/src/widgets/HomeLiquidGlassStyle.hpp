#pragma once

#include <nxui/core/Renderer.hpp>

namespace switchu::homeui {

inline void applyLiquidGlassV102(nxui::Renderer& ren) {
    auto& lg = ren.liquidGlassSettings();

    // HOME V10.2: stronger lens-like refraction derived from the existing
    // OverShifted/LiquidGlass-inspired nxui shader. Kept deliberately static
    // (no animation) so the HUD feels like real glass rather than a wobbling FX.
    lg.refractionIntensity = 0.16f;
    lg.blurIntensity = 3.15f;
    lg.noiseIntensity = 0.012f;
    lg.glowIntensity = 0.42f;
    lg.saturation = 1.04f;
    lg.opacityMultiplier = 0.34f;
    lg.roughness = 0.012f;
    lg.animSpeed = 0.0f;
    lg.time = 0.0f;
    lg.powerFactor = 5.25f;
    lg.fPower = 1.16f;
    lg.refA = 0.74f;
    lg.refB = 2.42f;
    lg.refC = 5.15f;
    lg.refD = 6.55f;
    lg.glowWeight = 0.20f;
    lg.glowBias = 0.015f;
    lg.glowEdge0 = 0.16f;
    lg.glowEdge1 = -0.06f;
    lg.tintBoost = nxui::Color(0.92f, 1.06f, 0.98f, 0.90f);
}

} // namespace switchu::homeui
