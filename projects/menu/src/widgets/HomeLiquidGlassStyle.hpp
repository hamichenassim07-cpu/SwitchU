#pragma once

#include <nxui/core/Renderer.hpp>
#include <chrono>
#include <fstream>

namespace switchu::homeui {

inline bool liquidGlassDebugFlagEnabled() {
    static const bool enabled = [] {
        std::ifstream f("sdmc:/config/SwitchU/liquid_glass_debug.flag", std::ios::binary);
        return f.good();
    }();
    return enabled;
}

inline float liquidGlassTimeSeconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    return std::chrono::duration<float>(Clock::now() - start).count();
}

inline void applyLiquidGlassV105(nxui::Renderer& ren) {
    auto& lg = ren.liquidGlassSettings();

    // V10.4 C2: the shader itself now performs edge-normal refraction,
    // convex lensing, two frost bands, RGB dispersion, Fresnel/specular light,
    // an inner shadow and a tiny animated liquid warp. These values are kept
    // intentionally balanced so the material looks like glass rather than a
    // translucent blue card.
    lg.refractionIntensity = 0.74f;
    lg.blurIntensity = 4.90f;
    lg.noiseIntensity = 0.012f;
    lg.glowIntensity = 0.92f;
    lg.saturation = 1.11f;
    lg.opacityMultiplier = 0.40f;
    lg.roughness = 0.011f;
    lg.animSpeed = 0.24f;
    lg.time = liquidGlassTimeSeconds();
    lg.powerFactor = 6.90f;
    lg.fPower = 1.38f;
    lg.refA = 0.88f;
    lg.refB = 2.60f;
    lg.refC = 5.90f;
    lg.refD = 7.15f;
    lg.glowWeight = 0.38f;
    lg.glowBias = 0.018f;
    lg.glowEdge0 = 0.20f;
    lg.glowEdge1 = -0.10f;
    lg.tintBoost = nxui::Color(1.03f, 1.07f, 1.13f, 0.96f);

    // Diagnostic mode is deliberately extreme. It is enabled simply by
    // creating sdmc:/config/SwitchU/liquid_glass_debug.flag BEFORE launching
    // Switch U. The shader recognizes refraction >= .95 and exaggerates the
    // lens + RGB split, making it impossible to confuse with the old shader.
    if (liquidGlassDebugFlagEnabled()) {
        lg.refractionIntensity = 1.00f;
        lg.blurIntensity = 5.60f;
        lg.noiseIntensity = 0.020f;
        lg.glowIntensity = 1.12f;
        lg.saturation = 1.28f;
        lg.opacityMultiplier = 0.42f;
        lg.roughness = 0.026f;
        lg.animSpeed = 0.34f;
        lg.powerFactor = 7.30f;
        lg.fPower = 1.42f;
        lg.glowWeight = 0.44f;
        lg.glowBias = 0.025f;
    }
}

// Compatibility names retained so any older V10.x source file still calls the
// V10.5 implementation when this header is overlaid onto the project.
inline void applyLiquidGlassV104(nxui::Renderer& ren) { applyLiquidGlassV105(ren); }
inline void applyLiquidGlassV102(nxui::Renderer& ren) { applyLiquidGlassV105(ren); }

} // namespace switchu::homeui
