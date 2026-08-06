#include "LockPressIndicator.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace {

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

// nxui does not expose a native stroked path, so the arc is approximated
// with many very short line sections. V6.1 uses significantly more sections
// than V6: the joins become nearly invisible on the real 720p display.
void drawArc(nxui::Renderer& ren,
             const nxui::Vec2& center,
             float radius,
             float startAngle,
             float endAngle,
             const nxui::Color& color,
             float thickness,
             int sections,
             bool roundedCaps) {
    if (radius <= 0.f || thickness <= 0.f)
        return;

    sections = std::max(24, sections);
    nxui::Vec2 previous = pointOnCircle(center, radius, startAngle);
    for (int i = 1; i <= sections; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sections);
        const float angle = startAngle + (endAngle - startAngle) * t;
        const nxui::Vec2 current = pointOnCircle(center, radius, angle);
        ren.drawLine(previous, current, color, thickness);
        previous = current;
    }

    if (roundedCaps) {
        const float capRadius = thickness * 0.50f;
        ren.drawCircle(pointOnCircle(center, radius, startAngle), capRadius, color, 28);
        ren.drawCircle(pointOnCircle(center, radius, endAngle), capRadius, color, 28);
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
    const float radius = std::min(r.width, r.height) * 0.394f;
    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.70f);

    // Activation order: cyan at the top, blue-violet on the left,
    // then pink on the right.
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

        // Slightly thicker inactive track than V6.
        const nxui::Color track = m_theme
            ? m_theme->pageIndicator.withAlpha((0.17f + (next ? 0.045f * breathe : 0.f)) * alpha)
            : nxui::Color(0.22f, 0.27f, 0.48f,
                          (0.17f + (next ? 0.045f * breathe : 0.f)) * alpha);
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                track, 24.f, 128, true);

        if (!active)
            continue;

        const float flashBoost = 1.f + localFlash * 0.10f;
        const nxui::Color activeColor = segment.color.withAlpha(
            (0.92f + 0.08f * breathe) * alpha
        );

        // One controlled bloom only. V6 used several wide strokes, which
        // made the construction look visibly stacked.
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                segment.color.withAlpha((0.085f + 0.045f * localFlash) * alpha),
                49.f * flashBoost, 120, true);

        // Main body: thicker, cleaner, and sampled with many more sections.
        drawArc(ren, center, radius,
                segment.startAngle, segment.endAngle,
                activeColor, 31.f * flashBoost, 144, true);

        // Narrow inner reflection, kept subtle so it does not read as a
        // second stacked ring.
        drawArc(ren, center, radius - 2.2f,
                segment.startAngle + 0.018f,
                segment.endAngle - 0.018f,
                nxui::Color(0.90f, 0.98f, 1.f,
                            (0.20f + 0.12f * localFlash) * alpha),
                3.4f, 136, true);
    }
}
