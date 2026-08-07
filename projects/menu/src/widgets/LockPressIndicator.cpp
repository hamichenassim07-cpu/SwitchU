#include "LockPressIndicator.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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

// V6.3: a segment is built as one non-overlapping mesh. The semicircular
// endings meet the annular body on the same radial edge instead of placing a
// transparent circle on top of it. This removes the brighter half-moons that
// were visible at the ends in V6.2.
void drawContinuousArc(nxui::Renderer& ren,
                       const nxui::Vec2& center,
                       float radius,
                       float startAngle,
                       float endAngle,
                       const nxui::Color& color,
                       float thickness,
                       int sections,
                       bool roundedEnds = true) {
    if (radius <= 0.f || thickness <= 0.f || endAngle <= startAngle)
        return;

    sections = std::max(20, sections);
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

    if (!roundedEnds)
        return;

    constexpr int kCapSections = 22;
    auto drawCap = [&](float angle, bool startCap) {
        const nxui::Vec2 capCenter = pointOnCircle(center, radius, angle);
        const nxui::Vec2 radial{std::cos(angle), std::sin(angle)};
        const nxui::Vec2 tangent{-std::sin(angle), std::cos(angle)};

        // Outer radial edge -> inner radial edge. At the start the cap grows
        // against the arc direction; at the end it grows with it.
        for (int i = 0; i < kCapSections; ++i) {
            const float u0 = static_cast<float>(i) / kCapSections;
            const float u1 = static_cast<float>(i + 1) / kCapSections;
            const float p0 = startCap ? -kPi * u0 : kPi * u0;
            const float p1 = startCap ? -kPi * u1 : kPi * u1;

            const nxui::Vec2 edge0{
                capCenter.x + half * (std::cos(p0) * radial.x + std::sin(p0) * tangent.x),
                capCenter.y + half * (std::cos(p0) * radial.y + std::sin(p0) * tangent.y)
            };
            const nxui::Vec2 edge1{
                capCenter.x + half * (std::cos(p1) * radial.x + std::sin(p1) * tangent.x),
                capCenter.y + half * (std::cos(p1) * radial.y + std::sin(p1) * tangent.y)
            };
            ren.drawTriangle(capCenter, edge0, edge1, color);
        }
    };

    drawCap(startAngle, true);
    drawCap(endAngle, false);
}

#ifdef NXUI_BACKEND_DEKO3D
void writeOrthoProjection(nxui::Renderer& ren, float width, float height) {
    nxui::VsUniforms vs{};
    vs.projection[0] = 2.f / width;
    vs.projection[5] = -2.f / height;
    vs.projection[10] = -1.f;
    vs.projection[12] = -1.f;
    vs.projection[13] = 1.f;
    vs.projection[15] = 1.f;

    auto& gpu = ren.gpu();
    const int slot = gpu.slot();
    std::memcpy(gpu.vsUboCpuAddr(slot), &vs, sizeof(vs));
    gpu.cmdBuf().bindUniformBuffer(DkStage_Vertex, 0,
                                   gpu.vsUboGpuAddr(slot),
                                   nxui::GpuDevice::VS_UBO_SIZE);
}

bool beginRingGlowTarget(nxui::Renderer& ren) {
    auto& gpu = ren.gpu();
    if (!gpu.offscreenReady())
        return false;

    ren.flush();
    auto cmd = gpu.cmdBuf();
    dk::ImageView colorTarget{gpu.offscreenImage(0)};
    cmd.bindRenderTargets(&colorTarget);

    constexpr uint32_t offW = nxui::GpuDevice::FB_WIDTH / 2;
    constexpr uint32_t offH = nxui::GpuDevice::FB_HEIGHT / 2;
    cmd.setViewports(0, DkViewport{0.f, 0.f, (float)offW, (float)offH, 0.f, 1.f});
    cmd.setScissors(0, DkScissor{0, 0, offW, offH});
    cmd.clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 0.f);
    writeOrthoProjection(ren, (float)offW, (float)offH);
    ren.useShader(nxui::ShaderProgram::Basic);
    return true;
}

void endRingGlowTarget(nxui::Renderer& ren) {
    ren.flush();
    auto& gpu = ren.gpu();
    auto cmd = gpu.cmdBuf();
    cmd.barrier(DkBarrier_Full, DkInvalidateFlags_Image);

    const int slot = gpu.slot();
    dk::ImageView colorTarget{gpu.fbImage(slot)};
    dk::ImageView dsTarget{gpu.dsImage()};
    cmd.bindRenderTargets(&colorTarget, &dsTarget);
    cmd.setViewports(0, DkViewport{0.f, 0.f,
        (float)gpu.width(), (float)gpu.height(), 0.f, 1.f});
    cmd.setScissors(0, DkScissor{0, 0,
        (uint32_t)gpu.width(), (uint32_t)gpu.height()});
    writeOrthoProjection(ren, (float)gpu.width(), (float)gpu.height());
}
#endif

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
    const float radius = std::min(r.width, r.height) * 0.405f;
    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.62f);

    const std::array<RingSegment, 3> segments = {{
        {3.70f, 5.72f, nxui::Color(0.05f, 0.88f, 1.00f, 1.f)},
        {1.82f, 3.08f, nxui::Color(0.30f, 0.42f, 1.00f, 1.f)},
        {0.06f, 1.32f, nxui::Color(1.00f, 0.22f, 0.86f, 1.f)},
    }};

    float strongestFlash = 0.f;
    bool hasActiveSegment = false;
    for (int i = 0; i < 3; ++i) {
        if (i < m_progress) {
            hasActiveSegment = true;
            if (i == m_progress - 1)
                strongestFlash = std::clamp(m_flash, 0.f, 1.f);
        }
    }

    bool usedRealBlur = false;
#ifdef NXUI_BACKEND_DEKO3D
    // V6.5 real post-process glow. The validated ring geometry is rendered
    // once into a transparent half-resolution target, blurred horizontally
    // and vertically by the existing nxui pipeline, then recomposited behind
    // the untouched sharp ring. Only the light changes; the ring shape does not.
    if (hasActiveSegment && beginRingGlowTarget(ren)) {
        const nxui::Vec2 glowCenter{center.x * 0.5f, center.y * 0.5f};
        const float glowRadius = radius * 0.5f;

        for (int i = 0; i < 3; ++i) {
            if (i >= m_progress)
                continue;

            const float localFlash = i == m_progress - 1
                ? strongestFlash
                : 0.f;
            const auto& segment = segments[static_cast<std::size_t>(i)];
            const float sourceThickness = (38.f + 7.f * localFlash) * 0.5f;

            drawContinuousArc(
                ren, glowCenter, glowRadius,
                segment.startAngle, segment.endAngle,
                segment.color.withAlpha(
                    (0.72f + 0.12f * breathe + 0.14f * localFlash) * alpha),
                sourceThickness, 112);

            drawContinuousArc(
                ren, glowCenter, glowRadius - 2.2f,
                segment.startAngle + 0.025f,
                segment.endAngle - 0.025f,
                nxui::Color(0.92f, 0.98f, 1.f,
                            (0.22f + 0.20f * localFlash) * alpha),
                5.0f, 96, false);
        }

        endRingGlowTarget(ren);
        ren.applyBlur(2.65f, 2);
        const float compositeAlpha = std::clamp(
            0.56f + 0.10f * breathe + 0.22f * strongestFlash,
            0.f, 0.92f);
        // The blur target is 640x360. On hardware, composing it into a
        // 1280x720 destination caused the source to be interpreted once more
        // at half scale, producing a smaller copy shifted toward the top-left.
        // A 2x destination compensates that sampling path; the screen viewport
        // clips the excess and puts the glow exactly behind the sharp ring.
        ren.drawOffscreen(0,
                          {0.f, 0.f,
                           (float)ren.width() * 2.f,
                           (float)ren.height() * 2.f},
                          nxui::Color::white().withAlpha(compositeAlpha * alpha));
        usedRealBlur = true;
    }
#endif

    for (int i = 0; i < 3; ++i) {
        const bool active = i < m_progress;
        const bool next = i == m_progress && m_progress < 3;
        const float localFlash = active && i == m_progress - 1
            ? std::clamp(m_flash, 0.f, 1.f)
            : 0.f;
        const RingSegment& segment = segments[static_cast<std::size_t>(i)];

        const nxui::Color track = m_theme
            ? m_theme->pageIndicator.withAlpha(
                  (0.17f + (next ? 0.060f * breathe : 0.f)) * alpha)
            : nxui::Color(0.22f, 0.27f, 0.48f,
                          (0.17f + (next ? 0.060f * breathe : 0.f)) * alpha);

        drawContinuousArc(ren, center, radius,
                          segment.startAngle, segment.endAngle,
                          track, 27.f, 108);

        if (!active)
            continue;

        const float flashBoost = 1.f + localFlash * 0.07f;

        // Portable fallback when offscreen post-processing is unavailable.
        if (!usedRealBlur) {
            drawContinuousArc(ren, center, radius,
                              segment.startAngle, segment.endAngle,
                              segment.color.withAlpha(
                                  (0.045f + 0.022f * breathe +
                                   0.050f * localFlash) * alpha),
                              52.f * flashBoost, 116);
            drawContinuousArc(ren, center, radius,
                              segment.startAngle, segment.endAngle,
                              segment.color.withAlpha(
                                  (0.090f + 0.028f * breathe +
                                   0.060f * localFlash) * alpha),
                              42.f * flashBoost, 120);
        }

        // Main body: unchanged geometry and dimensions from the validated ring.
        drawContinuousArc(ren, center, radius,
                          segment.startAngle, segment.endAngle,
                          segment.color.withAlpha(
                              (0.90f + 0.08f * breathe) * alpha),
                          34.f * flashBoost, 128);

        // Thin inner reflection retained for material depth.
        drawContinuousArc(ren, center, radius - 8.0f,
                          segment.startAngle + 0.035f,
                          segment.endAngle - 0.035f,
                          nxui::Color(0.92f, 0.98f, 1.f,
                                      (0.12f + 0.08f * breathe +
                                       0.12f * localFlash) * alpha),
                          3.2f, 104, false);

        if (i == m_progress - 1) {
            const float span = segment.endAngle - segment.startAngle;
            const float phase = std::fmod(m_pulse * 0.28f, 1.f);
            const float sheenStart = segment.startAngle +
                span * (0.08f + 0.70f * phase);
            const float sheenEnd = std::min(segment.endAngle - 0.04f,
                                            sheenStart + span * 0.17f);
            if (sheenEnd > sheenStart) {
                drawContinuousArc(ren, center, radius - 4.5f,
                                  sheenStart, sheenEnd,
                                  nxui::Color(1.f, 1.f, 1.f,
                                              (0.08f + 0.15f * localFlash) * alpha),
                                  5.0f, 30, false);
            }
        }
    }
}
