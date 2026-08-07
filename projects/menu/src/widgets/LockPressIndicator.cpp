#include "LockPressIndicator.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

nxui::Color mixColor(const nxui::Color& a,
                     const nxui::Color& b,
                     float t) {
    t = clamp01(t);
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

nxui::Color cardStageColor(float progress) {
    // The resting state is a calm blue. Each A press moves the glass edge
    // through cyan, deep blue-violet and finally fuchsia.
    const nxui::Color idle(0.08f, 0.46f, 0.92f, 1.f);
    const nxui::Color first(0.04f, 0.84f, 1.00f, 1.f);
    const nxui::Color second(0.18f, 0.30f, 1.00f, 1.f);
    const nxui::Color third(1.00f, 0.12f, 0.68f, 1.f);

    progress = std::clamp(progress, 0.f, 3.f);
    if (progress <= 1.f)
        return mixColor(idle, first, progress);
    if (progress <= 2.f)
        return mixColor(first, second, progress - 1.f);
    return mixColor(second, third, progress - 2.f);
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

bool beginCardGlowTarget(nxui::Renderer& ren) {
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

void endCardGlowTarget(nxui::Renderer& ren) {
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
    const float alpha = clamp01(opacity());
    if (alpha <= 0.001f || r.width <= 1.f || r.height <= 1.f)
        return;

    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.38f);
    const float flash = clamp01(m_flash);
    const nxui::Color stage = cardStageColor(m_visualProgress);
    const float radius = std::min(r.width, r.height) * 0.135f;
    const bool projected = m_outlineCount >= 3;

    auto drawProjectedLines = [&](float coordinateScale,
                                  const nxui::Color& color,
                                  float thickness) {
        if (!projected)
            return;
        for (int i = 0; i < m_outlineCount; ++i) {
            const int j = (i + 1) % m_outlineCount;
            ren.drawLine(
                {m_outline[i].x * coordinateScale,
                 m_outline[i].y * coordinateScale},
                {m_outline[j].x * coordinateScale,
                 m_outline[j].y * coordinateScale},
                color,
                thickness * coordinateScale);
        }
    };

    bool usedRealBlur = false;
#ifdef NXUI_BACKEND_DEKO3D
    if (beginCardGlowTarget(ren)) {
        if (projected) {
            // The old glow is preserved, but its source now follows the actual
            // perspective silhouette instead of an axis-aligned rectangle.
            drawProjectedLines(
                0.5f,
                stage.withAlpha((0.72f + 0.10f * breathe +
                                 0.18f * flash) * alpha),
                18.f + 4.f * flash);
            drawProjectedLines(
                0.5f,
                nxui::Color(0.94f, 0.98f, 1.f,
                            (0.18f + 0.16f * flash) * alpha),
                4.0f);
        } else {
            const nxui::Rect halfRect = {
                r.x * 0.5f,
                r.y * 0.5f,
                r.width * 0.5f,
                r.height * 0.5f
            };
            const float halfRadius = radius * 0.5f;

            ren.drawRoundedRectOutline(
                halfRect.expanded(3.5f),
                stage.withAlpha((0.68f + 0.10f * breathe +
                                 0.18f * flash) * alpha),
                halfRadius + 3.5f,
                15.f + 3.f * flash);
            ren.drawRoundedRectOutline(
                halfRect,
                nxui::Color(0.94f, 0.98f, 1.f,
                            (0.18f + 0.16f * flash) * alpha),
                halfRadius,
                3.2f);
        }

        endCardGlowTarget(ren);
        ren.applyBlur(2.50f, 2);
        const float compositeAlpha = std::clamp(
            0.40f + 0.08f * breathe + 0.18f * flash,
            0.f, 0.82f);
        ren.drawOffscreen(0,
                          {0.f, 0.f,
                           (float)ren.width() * 2.f,
                           (float)ren.height() * 2.f},
                          nxui::Color::white().withAlpha(compositeAlpha * alpha));
        usedRealBlur = true;
    }
#endif

    if (!usedRealBlur) {
        if (projected) {
            drawProjectedLines(
                1.f,
                stage.withAlpha((0.040f + 0.035f * breathe +
                                 0.065f * flash) * alpha),
                27.f + 5.f * flash);
            drawProjectedLines(
                1.f,
                stage.withAlpha((0.080f + 0.035f * breathe +
                                 0.080f * flash) * alpha),
                14.f + 3.f * flash);
        } else {
            ren.drawRoundedRectOutline(
                r.expanded(12.f + 3.f * flash),
                stage.withAlpha((0.025f + 0.030f * breathe +
                                 0.055f * flash) * alpha),
                radius + 12.f,
                24.f + 5.f * flash);
            ren.drawRoundedRectOutline(
                r.expanded(6.f),
                stage.withAlpha((0.055f + 0.035f * breathe +
                                 0.070f * flash) * alpha),
                radius + 6.f,
                14.f + 3.f * flash);
        }
    }

    if (projected) {
        drawProjectedLines(
            1.f,
            nxui::Color(0.34f, 0.48f, 0.76f,
                        (0.17f + 0.030f * breathe) * alpha),
            2.4f);
        drawProjectedLines(
            1.f,
            stage.withAlpha((0.68f + 0.13f * breathe +
                             0.10f * flash) * alpha),
            3.2f + 1.2f * flash);
        return;
    }

    // Rectangle fallback kept for the no-game layout and older call sites.
    ren.drawRoundedRectOutline(
        r,
        nxui::Color(0.34f, 0.48f, 0.76f,
                    (0.20f + 0.035f * breathe) * alpha),
        radius,
        3.0f);
    ren.drawRoundedRectOutline(
        r,
        stage.withAlpha((0.78f + 0.14f * breathe +
                         0.08f * flash) * alpha),
        radius,
        4.0f + 1.5f * flash);
    ren.drawRoundedRectOutline(
        r.shrunk(3.4f),
        nxui::Color(0.96f, 0.99f, 1.f,
                    (0.12f + 0.10f * flash) * alpha),
        std::max(2.f, radius - 3.4f),
        1.25f);
}
