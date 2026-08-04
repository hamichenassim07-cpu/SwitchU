#include "SelectionCursor.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

nxui::Color mixColor(const nxui::Color& a,
                     const nxui::Color& b,
                     float t) {
    t = std::clamp(t, 0.f, 1.f);

    return nxui::Color(
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    );
}

nxui::Color gradientColor(float t) {
    t = std::fmod(t, 1.f);
    if (t < 0.f)
        t += 1.f;

    const nxui::Color purple(0.38f, 0.08f, 0.82f, 1.f);
    const nxui::Color darkFuchsia(0.74f, 0.08f, 0.54f, 1.f);
    const nxui::Color electricBlue(0.08f, 0.58f, 1.00f, 1.f);

    if (t < 1.f / 3.f)
        return mixColor(purple, darkFuchsia, t * 3.f);

    if (t < 2.f / 3.f)
        return mixColor(darkFuchsia, electricBlue, (t - 1.f / 3.f) * 3.f);

    return mixColor(electricBlue, purple, (t - 2.f / 3.f) * 3.f);
}

nxui::Vec2 roundedPerimeterPoint(const nxui::Rect& rect,
                                 float radius,
                                 float u) {
    const float w = std::max(1.f, rect.width);
    const float h = std::max(1.f, rect.height);
    const float r = std::clamp(radius, 1.f, std::min(w, h) * 0.5f);

    const float horizontal = std::max(0.f, w - 2.f * r);
    const float vertical = std::max(0.f, h - 2.f * r);
    const float quarterArc = 0.5f * kPi * r;
    const float perimeter =
        2.f * horizontal + 2.f * vertical + 4.f * quarterArc;

    float distance = std::fmod(std::max(0.f, u), 1.f) * perimeter;

    if (distance <= horizontal)
        return {rect.x + r + distance, rect.y};

    distance -= horizontal;

    if (distance <= quarterArc) {
        const float angle = -0.5f * kPi + distance / r;
        return {
            rect.x + w - r + std::cos(angle) * r,
            rect.y + r + std::sin(angle) * r
        };
    }

    distance -= quarterArc;

    if (distance <= vertical)
        return {rect.x + w, rect.y + r + distance};

    distance -= vertical;

    if (distance <= quarterArc) {
        const float angle = distance / r;
        return {
            rect.x + w - r + std::cos(angle) * r,
            rect.y + h - r + std::sin(angle) * r
        };
    }

    distance -= quarterArc;

    if (distance <= horizontal)
        return {rect.x + w - r - distance, rect.y + h};

    distance -= horizontal;

    if (distance <= quarterArc) {
        const float angle = 0.5f * kPi + distance / r;
        return {
            rect.x + r + std::cos(angle) * r,
            rect.y + h - r + std::sin(angle) * r
        };
    }

    distance -= quarterArc;

    if (distance <= vertical)
        return {rect.x, rect.y + h - r - distance};

    distance -= vertical;

    const float angle = kPi + distance / r;
    return {
        rect.x + r + std::cos(angle) * r,
        rect.y + r + std::sin(angle) * r
    };
}

void drawAnimatedGradientOutline(nxui::Renderer& ren,
                                 const nxui::Rect& rect,
                                 float radius,
                                 float thickness,
                                 float alpha,
                                 float phase) {
    constexpr int kSegments = 112;

    nxui::Vec2 previous = roundedPerimeterPoint(rect, radius, 0.f);

    for (int i = 1; i <= kSegments; ++i) {
        const float u = static_cast<float>(i) /
                        static_cast<float>(kSegments);

        nxui::Vec2 current =
            roundedPerimeterPoint(rect, radius, u);

        nxui::Color color =
            gradientColor(u + phase).withAlpha(alpha);

        ren.drawLine(previous, current, color, thickness);
        previous = current;
    }
}

} // namespace

SelectionCursor::SelectionCursor() {
    m_x.setImmediate(0);
    m_y.setImmediate(0);
    m_w.setImmediate(0);
    m_h.setImmediate(0);
    m_cornerRadius.setImmediate(18.f);
}

float SelectionCursor::computeAdaptiveDuration(const nxui::Rect& target,
                                               float targetCornerRadius,
                                               float baseDuration) const {
    float sx = m_x.value();
    float sy = m_y.value();
    float sw = std::max(1.f, m_w.value());
    float sh = std::max(1.f, m_h.value());
    float sr = m_cornerRadius.value();

    float sourceCx = sx + sw * 0.5f;
    float sourceCy = sy + sh * 0.5f;
    float targetCx = target.x + target.width * 0.5f;
    float targetCy = target.y + target.height * 0.5f;

    float dx = targetCx - sourceCx;
    float dy = targetCy - sourceCy;
    float distance = std::sqrt(dx * dx + dy * dy);

    float sourceDiag = std::sqrt(sw * sw + sh * sh);
    float targetW = std::max(1.f, target.width);
    float targetH = std::max(1.f, target.height);
    float targetDiag = std::sqrt(targetW * targetW + targetH * targetH);

    float sizeDelta =
        std::abs(targetDiag - sourceDiag) /
        std::max(1.f, sourceDiag);

    float sourceAspect = sw / sh;
    float targetAspect = targetW / targetH;

    float aspectDelta =
        std::abs(targetAspect - sourceAspect) /
        std::max(0.35f, sourceAspect);

    float radiusNorm =
        std::max(4.f,
                 std::max(std::abs(sr), std::abs(targetCornerRadius)));

    float radiusDelta =
        std::abs(targetCornerRadius - sr) / radiusNorm;

    float distFactor =
        std::clamp(distance / 320.f, 0.f, 2.4f);

    float deformFactor =
        std::clamp(sizeDelta + aspectDelta + radiusDelta, 0.f, 1.8f);

    float scale =
        1.f + distFactor * 0.50f + deformFactor * 0.35f;

    float adaptive = baseDuration * scale;

    return std::clamp(
        adaptive,
        baseDuration * 0.95f,
        baseDuration * 2.8f
    );
}

void SelectionCursor::moveTo(const nxui::Rect& target, float duration) {
    if (!m_initialized) {
        m_x.setImmediate(target.x);
        m_y.setImmediate(target.y);
        m_w.setImmediate(target.width);
        m_h.setImmediate(target.height);
        m_initialized = true;
        return;
    }

    constexpr float eps = 0.5f;

    if (std::abs(m_x.target() - target.x) < eps &&
        std::abs(m_y.target() - target.y) < eps &&
        std::abs(m_w.target() - target.width) < eps &&
        std::abs(m_h.target() - target.height) < eps)
        return;

    float adaptiveDuration =
        computeAdaptiveDuration(
            target,
            m_cornerRadius.value(),
            duration
        );

    m_x.set(target.x, adaptiveDuration, nxui::Easing::outCubic);
    m_y.set(target.y, adaptiveDuration, nxui::Easing::outCubic);
    m_w.set(target.width, adaptiveDuration, nxui::Easing::outCubic);
    m_h.set(target.height, adaptiveDuration, nxui::Easing::outCubic);
}

void SelectionCursor::moveTo(const nxui::Rect& target,
                             float cornerRadius,
                             float duration) {
    if (!m_initialized) {
        m_x.setImmediate(target.x);
        m_y.setImmediate(target.y);
        m_w.setImmediate(target.width);
        m_h.setImmediate(target.height);
        m_cornerRadius.setImmediate(cornerRadius);
        m_initialized = true;
        return;
    }

    constexpr float eps = 0.5f;

    if (std::abs(m_x.target() - target.x) < eps &&
        std::abs(m_y.target() - target.y) < eps &&
        std::abs(m_w.target() - target.width) < eps &&
        std::abs(m_h.target() - target.height) < eps &&
        std::abs(m_cornerRadius.target() - cornerRadius) < eps)
        return;

    float adaptiveDuration =
        computeAdaptiveDuration(
            target,
            cornerRadius,
            duration
        );

    m_x.set(target.x, adaptiveDuration, nxui::Easing::outCubic);
    m_y.set(target.y, adaptiveDuration, nxui::Easing::outCubic);
    m_w.set(target.width, adaptiveDuration, nxui::Easing::outCubic);
    m_h.set(target.height, adaptiveDuration, nxui::Easing::outCubic);
    m_cornerRadius.set(
        cornerRadius,
        adaptiveDuration,
        nxui::Easing::outCubic
    );
}

nxui::Rect SelectionCursor::currentRect() const {
    return nxui::Rect{
        std::roundf(m_x.value()),
        std::roundf(m_y.value()),
        std::roundf(m_w.value()),
        std::roundf(m_h.value())
    };
}

void SelectionCursor::onUpdate(float dt) {
    m_time += dt;
}

void SelectionCursor::onRender(nxui::Renderer& ren) {
    if (!m_initialized || m_opacity <= 0.01f)
        return;

    const float x = m_x.value();
    const float y = m_y.value();
    const float w = m_w.value();
    const float h = m_h.value();

    if (w < 1.f || h < 1.f)
        return;

    const nxui::Rect r = {x, y, w, h};
    const float cr = m_cornerRadius.value();

    if (!m_gradientEnabled) {
        const float wave =
            0.5f + 0.5f * std::sin(m_time * 2.2f);

        ren.drawRoundedRectOutline(
            r.expanded(3.5f),
            m_color.withAlpha(
                (0.12f + 0.04f * wave) * m_opacity
            ),
            cr + 3.5f,
            m_borderWidth + 3.f
        );

        ren.drawRoundedRectOutline(
            r,
            m_color.withAlpha(0.92f * m_opacity),
            cr,
            m_borderWidth
        );

        return;
    }

    const float breathe =
        0.5f + 0.5f * std::sin(m_time * 1.35f);

    // Very slow movement: approximately one full colour rotation
    // every 32 seconds.
    const float phase = std::fmod(m_time * 0.03125f, 1.f);

    // Small and discreet outer glow.
    ren.drawRoundedRectOutline(
        r.expanded(7.f + breathe * 1.5f),
        nxui::Color(
            0.42f,
            0.10f,
            0.82f,
            (0.035f + 0.015f * breathe) * m_opacity
        ),
        cr + 8.f,
        7.f
    );

    ren.drawRoundedRectOutline(
        r.expanded(3.5f),
        nxui::Color(
            0.08f,
            0.50f,
            1.00f,
            (0.055f + 0.020f * breathe) * m_opacity
        ),
        cr + 4.f,
        4.f
    );

    // Actual animated purple -> dark fuchsia -> blue gradient.
    drawAnimatedGradientOutline(
        ren,
        r.expanded(1.3f),
        cr + 1.3f,
        4.8f,
        0.96f * m_opacity,
        phase
    );

    // Fine inner reflection, deliberately more discreet.
    drawAnimatedGradientOutline(
        ren,
        r.shrunk(2.4f),
        std::max(1.f, cr - 2.4f),
        2.4f,
        (0.40f + 0.08f * breathe) * m_opacity,
        phase + 0.17f
    );
}
