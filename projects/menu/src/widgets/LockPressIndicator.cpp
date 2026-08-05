#include "LockPressIndicator.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

void LockPressIndicator::onRender(nxui::Renderer& ren) {
    const nxui::Rect r = rect();
    const float alpha = std::clamp(opacity(), 0.f, 1.f);
    if (alpha <= 0.001f)
        return;

    constexpr float dotRadius = 6.f;
    constexpr float gap = 20.f;
    const float dotsWidth = dotRadius * 6.f + gap * 2.f;
    const float startX = r.x + (r.width - dotsWidth) * 0.5f + dotRadius;
    const float cy = r.y + r.height * 0.5f;
    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.35f);

    const nxui::Color inactive = m_theme
        ? m_theme->pageIndicator
        : nxui::Color(0.62f, 0.68f, 0.82f, 0.32f);
    static const nxui::Color activeColors[3] = {
        nxui::Color(0.18f, 0.70f, 1.00f, 0.98f),
        nxui::Color(0.38f, 0.48f, 1.00f, 0.98f),
        nxui::Color(0.68f, 0.30f, 1.00f, 0.98f)
    };

    for (int i = 0; i < 3; ++i) {
        const nxui::Vec2 center = {
            startX + static_cast<float>(i) * (dotRadius * 2.f + gap),
            cy
        };
        const bool completed = i < m_progress;
        const bool next = i == m_progress && m_progress < 3;
        const float localFlash = completed && i == m_progress - 1 ? m_flash : 0.f;

        if (next || localFlash > 0.001f) {
            ren.drawCircle(
                center,
                13.f + localFlash * 5.f,
                nxui::Color(0.28f, 0.66f, 1.f,
                            (0.045f + localFlash * 0.060f) * alpha),
                28
            );
        }

        ren.drawCircle(
            center,
            dotRadius + 2.f,
            nxui::Color(0.06f, 0.07f, 0.14f, 0.92f * alpha),
            24
        );

        const float radius = completed
            ? dotRadius + localFlash * 1.4f
            : (next ? 4.8f + 0.6f * breathe : 4.6f);
        nxui::Color color = completed
            ? activeColors[i].withAlpha(activeColors[i].a * alpha)
            : inactive.withAlpha(inactive.a * (next ? 1.15f : 0.72f) * alpha);
        ren.drawCircle(center, radius, color, 24);
    }
}
