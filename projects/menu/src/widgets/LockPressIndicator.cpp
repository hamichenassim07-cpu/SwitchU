#include "LockPressIndicator.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct RingSegment {
    float startAngle;
    float endAngle;
    nxui::Color color;
};

nxui::Vec2 pointOnCircle(const nxui::Vec2& center, float radius, float angle) {
    return {
        center.x + std::cos(angle) * radius,
        center.y + std::sin(angle) * radius
    };
}

void drawArc(nxui::Renderer& ren,
             const nxui::Vec2& center,
             float radius,
             float startAngle,
             float endAngle,
             const nxui::Color& color,
             float thickness,
             int segments,
             bool roundedCaps) {
    if (radius <= 0.f || thickness <= 0.f)
        return;

    segments = std::max(8, segments);
    nxui::Vec2 previous = pointOnCircle(center, radius, startAngle);
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = startAngle + (endAngle - startAngle) * t;
        const nxui::Vec2 current = pointOnCircle(center, radius, angle);
        ren.drawLine(previous, current, color, thickness);
        previous = current;
    }

    if (roundedCaps) {
        const float capRadius = thickness * 0.50f;
        ren.drawCircle(pointOnCircle(center, radius, startAngle), capRadius, color, 24);
        ren.drawCircle(pointOnCircle(center, radius, endAngle), capRadius, color, 24);
    }
}

} // namespace

void LockPressIndicator::onRender(nxui::Renderer& ren) {
    const nxui::Rect r = rect();
    const float alpha = std::clamp(opacity(), 0.f, 1.f);
    if (alpha <= 0.001f || r.width <= 1.f || r.height <= 1.f)
        return;

    const nxui::Vec2 center = {
        r.x + r.width * 0.5f,
        r.y + r.height * 0.5f
    };
    const float radius = std::min(r.width, r.height) * 0.395f;
    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.70f);

    // Order of activation: cyan top, blue-violet left, pink right.
    const std::array<RingSegment, 3> segments = {{
        {3.70f, 5.72f, nxui::Color(0.05f, 0.88f, 1.00f, 1.f)},
        {1.82f, 3.08f, nxui::Color(0.30f, 0.42f, 1.00f, 1.f)},
        {0.06f, 1.32f, nxui::Color(1.00f, 0.22f, 0.86f, 1.f)},
    }};

    for (int i = 0; i < 3; ++i) {
        const bool active = i < m_progress;
        const bool next = i == m_progress && m_progress < 3;
        const float localFlash = active && i == m_progress - 1
            ? std::clamp(m_flash, 0.f, 1.f)
            : 0.f;

        const RingSegment& segment = segments[static_cast<std::size_t>(i)];

        // Inactive track: still readable, but clearly unfilled.
        const nxui::Color track = m_theme
            ? m_theme->pageIndicator.withAlpha((0.14f + (next ? 0.05f * breathe : 0.f)) * alpha)
            : nxui::Color(0.22f, 0.27f, 0.48f,
                          (0.14f + (next ? 0.05f * breathe : 0.f)) * alpha);
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                track, 20.f, 54, true);

        if (!active)
            continue;

        const float flashBoost = 1.f + localFlash * 0.22f;
        const float activeAlpha = (0.90f + 0.10f * breathe) * alpha;
        const nxui::Color activeColor = segment.color.withAlpha(activeAlpha);

        // Wide soft bloom.
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                segment.color.withAlpha((0.055f + 0.055f * localFlash) * alpha),
                58.f * flashBoost, 54, true);
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                segment.color.withAlpha((0.11f + 0.08f * localFlash) * alpha),
                40.f * flashBoost, 54, true);

        // Main luminous body and highlight.
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                activeColor, 25.f * flashBoost, 60, true);
        drawArc(ren, center, radius - 2.5f,
                segment.startAngle + 0.015f, segment.endAngle - 0.015f,
                nxui::Color(0.88f, 0.98f, 1.f,
                            (0.30f + 0.18f * localFlash) * alpha),
                5.2f, 58, true);
    }

    // A very subtle circular guide keeps the central composition coherent.
    drawArc(ren, center, radius - 54.f, 0.f, 2.f * kPi,
            nxui::Color(0.28f, 0.46f, 0.92f, 0.11f * alpha),
            1.5f, 96, false);
}
