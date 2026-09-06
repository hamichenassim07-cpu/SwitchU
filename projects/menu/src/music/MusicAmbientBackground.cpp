#include "MusicAmbientBackground.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace switchu::menu::music {
void MusicAmbientBackground::draw(nxui::Renderer& ren, float alpha, float floorY) {
    if (alpha <= 0.f) return;
    if (!m_textureAttempted) {
        m_textureAttempted = true;
        constexpr int side = 64;
        std::array<uint8_t, side * side * 4> pixels{};
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const float dx = (x + 0.5f) * (2.f / side) - 1.f;
                const float dy = (y + 0.5f) * (2.f / side) - 1.f;
                const float t = std::max(0.f, 1.f - dx*dx - dy*dy);
                const size_t i = size_t(y * side + x) * 4u;
                pixels[i] = pixels[i + 1] = pixels[i + 2] = 255;
                pixels[i + 3] = uint8_t(std::round(t*t*t * 255.f));
            }
        }
        // One immutable 16 KiB image, linearly sampled by the existing HOME
        // renderer. No per-frame uploads, blur, capture, or render target.
        m_lightTexture.loadFromPixels(ren.gpu(), ren, pixels.data(), side, side);
    }

    const auto lights = m_motion.lights();
    ren.pushClipRect({0.f, 0.f, 1280.f, floorY});
    if (m_lightTexture.valid() && m_lightTexture.descriptorSlot() >= 0) {
        for (const auto& light : lights) {
            ren.drawTexture(&m_lightTexture,
                {light.x - light.width*0.5f, light.y - light.height*0.5f,
                 light.width, light.height},
                {light.colour.r, light.colour.g, light.colour.b, light.opacity * alpha});
        }
    } else {
        // A GPU allocation failure never makes the background static. This
        // texture-free fallback still moves; it adds only eight simple quads.
        for (const auto& light : lights) {
            const float y = std::clamp(light.y, 90.f, floorY - 60.f);
            const nxui::Color bright{light.colour.r, light.colour.g, light.colour.b,
                                     light.opacity * 0.16f * alpha};
            const nxui::Color clear{light.colour.r, light.colour.g, light.colour.b, 0.f};
            ren.drawGradientRect({0.f, 0.f, 1280.f, y}, clear, bright);
            ren.drawGradientRect({0.f, y, 1280.f, floorY - y}, bright, clear);
        }
    }
    // Feather the atmosphere around the unchanged system HUD and horizon.
    ren.drawGradientRect({0.f, 0.f, 1280.f, 150.f},
                         {0.009f,0.011f,0.016f,0.82f*alpha},
                         {0.009f,0.011f,0.016f,0.f});
    ren.drawGradientRect({0.f, floorY - 108.f, 1280.f, 108.f},
                         {0.009f,0.011f,0.016f,0.f},
                         {0.009f,0.011f,0.016f,0.88f*alpha});
    ren.popClipRect();
}
} // namespace switchu::menu::music
