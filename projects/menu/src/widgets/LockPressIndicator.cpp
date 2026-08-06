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

// Draws a genuine filled annular sector. Unlike the former implementation,
// this does not build the arc from many neighbouring line strokes. Adjacent
// triangles share the exact same vertices, so the body reads as one continuous
// shape and cannot expose black gaps between short line segments.
void drawFilledArc(nxui::Renderer& ren,
                   const nxui::Vec2& center,
                   float radius,
                   float startAngle,
                   float endAngle,
                   const nxui::Color& color,
                   float thickness,
                   int sections,
                   bool roundedCaps) {
    if (radius <= 0.f || thickness <= 0.f || endAngle <= startAngle)
        return;

    sections = std::max(18, sections);
    const float half = thickness * 0.5f;
    const float innerRadius = std::max(0.5f, radius - half);
    const float outerRadius = radius + half;

    for (int i = 0; i < sections; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(sections);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(sections);
        const float a0 = startAngle + (endAngle - startAngle) * t0;
        const float a1 = startAngle + (endAngle - startAngle) * t1;

        const nxui::Vec2 outer0 = pointOnCircle(center, outerRadius, a0);
        const nxui::Vec2 outer1 = pointOnCircle(center, outerRadius, a1);
        const nxui::Vec2 inner0 = pointOnCircle(center, innerRadius, a0);
        const nxui::Vec2 inner1 = pointOnCircle(center, innerRadius, a1);

        ren.drawTriangle(outer0, inner0, outer1, color);
        ren.drawTriangle(inner0, inner1, outer1, color);
    }

    if (roundedCaps) {
        const float capRadius = thickness * 0.5f;
        ren.drawCircle(pointOnCircle(center, radius, startAngle),
                       capRadius, color, 36);
        ren.drawCircle(pointOnCircle(center, radius, endAngle),
                       capRadius, color, 36);
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

        const nxui::Color track = m_theme
            ? m_theme->pageIndicator.withAlpha(
                  (0.18f + (next ? 0.055f * breathe : 0.f)) * alpha)
            : nxui::Color(0.22f, 0.27f, 0.48f,
                          (0.18f + (next ? 0.055f * breathe : 0.f)) * alpha);

        // Slightly thicker than V6.1, but still restrained.
        drawFilledArc(ren, center, radius,
                      segment.startAngle, segment.endAngle,
                      track, 26.f, 96, true);

        if (!active)
            continue;

        const float flashBoost = 1.f + localFlash * 0.08f;

        // One soft bloom behind one solid body. No highlight stroke and no
        // stack of several neighbouring outlines.
        drawFilledArc(ren, center, radius,
                      segment.startAngle, segment.endAngle,
                      segment.color.withAlpha(
                          (0.075f + 0.040f * localFlash) * alpha),
                      45.f * flashBoost, 96, true);

        drawFilledArc(ren, center, radius,
                      segment.startAngle, segment.endAngle,
                      segment.color.withAlpha(
                          (0.94f + 0.06f * breathe) * alpha),
                      33.f * flashBoost, 112, true);
    }
}
