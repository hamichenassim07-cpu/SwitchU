#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <cmath>
#include <algorithm>

GlossyIcon::GlossyIcon() {
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
    m_focusScale.setImmediate(1.f);
    m_focusGlow.setImmediate(0.f);
    setCornerRadius(16.f);
    setPadding(8.f);
    setLiquidGlassEnabled(true);
    setBlurEnabled(false);
}

void GlossyIcon::onFocusGained() {
    m_focused = true;
    m_focusScale.set(1.12f, 0.20f, nxui::Easing::outBack);
    m_focusGlow.set(1.f, 0.16f, nxui::Easing::outCubic);
}

void GlossyIcon::onFocusLost() {
    m_focused = false;
    m_focusScale.set(1.f, 0.20f, nxui::Easing::outCubic);
    m_focusGlow.set(0.f, 0.16f, nxui::Easing::outCubic);
}

void GlossyIcon::startAppear(float delay) {
    m_appearDelay = delay;
    m_appearTimer = 0.f;
    m_appearing = true;
    m_animScale.setImmediate(0.f);
    m_appearOpacity.setImmediate(0.f);
}

void GlossyIcon::forceVisible() {
    m_appearing = false;
    m_appearDelay = 0.f;
    m_appearTimer = 0.f;
    m_animScale.setImmediate(1.f);
    m_appearOpacity.setImmediate(1.f);
}

void GlossyIcon::onContentUpdate(float dt) {
    if (m_appearing) {
        m_appearTimer += dt;
        if (m_appearTimer >= m_appearDelay) {
            m_appearing = false;
            m_animScale.set(1.f, 0.4f, nxui::Easing::outExpo);
            m_appearOpacity.set(1.f, 0.3f, nxui::Easing::outExpo);
        }
    }

    m_suspendPulse += dt * 2.2f;
}

void GlossyIcon::onRender(nxui::Renderer& ren) {
    float externalScale = scale();
    float focusS = m_focusScale.value();
    float s = m_animScale.value() * externalScale * focusS;
    float a = m_appearOpacity.value();

    if (s < 0.01f || a < 0.01f)
        return;

    nxui::Rect savedRect = m_rect;
    nxui::Rect drawRect = savedRect;

    if (std::abs(s - 1.f) > 0.001f) {
        float w = savedRect.width * s;
        float h = savedRect.height * s;
        drawRect.x += (savedRect.width - w) * 0.5f;
        drawRect.y += (savedRect.height - h) * 0.5f;
        drawRect.width = w;
        drawRect.height = h;
        m_rect = drawRect;
    }

    setScale(1.f);
    float savedShade = liquidGlassShade();
    setLiquidGlassShade(m_notLaunchable ? 0.58f : 0.0f);
    float savedOp = m_opacity;
    m_opacity = a * savedOp;

    nxui::GlassWidget::onRender(ren);

    m_opacity = savedOp;
    m_rect = savedRect;
    setLiquidGlassShade(savedShade);
    setScale(externalScale);

    nxui::Rect r = drawRect;
    float rad = cornerRadius();
    float focusGlow = m_focusGlow.value();

    // V6: stronger glass reflections on every icon.
    ren.drawRoundedRect(
        {r.x + 6.f, r.y + 4.f, r.width - 12.f, r.height * 0.20f},
        nxui::Color(1.f, 1.f, 1.f, (0.12f + 0.04f * focusGlow) * a),
        rad
    );

    ren.drawRoundedRect(
        {r.x + 10.f, r.y + 10.f, r.width * 0.55f, r.height * 0.10f},
        nxui::Color(1.f, 1.f, 1.f, (0.11f + 0.05f * focusGlow) * a),
        std::max(6.f, rad - 4.f)
    );

    if (focusGlow > 0.01f && s > 0.5f) {
        // Keep only a very soft outside bloom; the main coloured effect now
        // happens inside the icon border itself.
        const float breathe =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.80f);

        ren.drawRoundedRect(
            r.expanded(3.5f + breathe * 0.8f),
            nxui::Color(
                1.f, 1.f, 1.f,
                (0.028f + 0.010f * breathe) * focusGlow * a
            ),
            rad + 4.f
        );
    }

    if (m_isGameCard && !m_notLaunchable && s > 0.5f) {
        float badgeW = 66.f * s;
        float badgeH = 48.f * s;
        float badgeX = r.x + 1.f * s;
        float badgeY = r.y + 6.f * s;

        if (m_gameCardTex && m_gameCardTex->valid()) {
            float cardInset = 1.f * s;
            float maxW = badgeW - cardInset * 2;
            float maxH = badgeH - cardInset * 2;
            float aspect = (float)m_gameCardTex->width() / (float)m_gameCardTex->height();
            float texW = maxW;
            float texH = maxH;

            if (aspect > maxW / maxH)
                texH = maxW / aspect;
            else
                texW = maxH * aspect;

            float texX = badgeX + (badgeW - texW) * 0.5f;
            float texY = badgeY + (badgeH - texH) * 0.5f;

            ren.drawTextureRounded(m_gameCardTex, {texX, texY, texW, texH}, 2.f * s,
                                   nxui::Color::white().withAlpha(0.98f * a));
        } else {
            float cardInset = 4.f * s;
            ren.drawRoundedRect({badgeX + cardInset, badgeY + cardInset,
                                 badgeW - cardInset * 2, badgeH - cardInset * 2},
                                nxui::Color(0.95f, 0.75f, 0.2f, 0.9f * a),
                                2.f * s);
        }
    }

    if (m_suspended && s > 0.5f) {
        float pulse = 0.5f + 0.5f * std::sin(m_suspendPulse);
        float glowAlpha = 0.35f + 0.25f * pulse;

        nxui::Color glow(0.18f, 0.85f, 0.45f, glowAlpha * a);
        ren.drawRoundedRectOutline(r.expanded(2.f), glow, rad + 2.f, 2.5f);

        float badgeSize = 26.f * s;
        float badgeX = r.x + r.width - badgeSize - 4.f * s;
        float badgeY = r.y + r.height - badgeSize - 4.f * s;
        nxui::Vec2 badgeCenter = {
            badgeX + badgeSize * 0.5f,
            badgeY + badgeSize * 0.5f
        };

        ren.drawCircle(badgeCenter, badgeSize * 0.5f,
                       nxui::Color(0.1f, 0.1f, 0.1f, 0.85f * a), 16);

        float triH = badgeSize * 0.45f;
        float triW = triH * 0.85f;
        nxui::Vec2 p1 = {badgeCenter.x - triW * 0.35f, badgeCenter.y - triH * 0.5f};
        nxui::Vec2 p2 = {badgeCenter.x - triW * 0.35f, badgeCenter.y + triH * 0.5f};
        nxui::Vec2 p3 = {badgeCenter.x + triW * 0.65f, badgeCenter.y};

        ren.drawTriangle(p1, p2, p3,
                         nxui::Color(0.18f, 0.85f, 0.45f, 0.95f * a));
    }
}

void GlossyIcon::onContentRender(nxui::Renderer& ren) {
    float s = scale();
    float rad = cornerRadius();
    nxui::Rect r = m_rect;

    if (s < 1.f) {
        float w = r.width * s;
        float h = r.height * s;
        r.x += (r.width - w) * 0.5f;
        r.y += (r.height - h) * 0.5f;
        r.width = w;
        r.height = h;
    }

    float inset = 8.f * s;
    nxui::Rect texRect = r.shrunk(inset);
    float texRadius = std::max(2.f, rad - 3.f);

    // Cleaner dark placeholder: no central round hotspot.
    if (!m_tex || !m_tex->valid()) {
        ren.drawRoundedRect(
            texRect,
            nxui::Color(0.05f, 0.04f, 0.11f, 0.76f * m_opacity),
            texRadius
        );

        ren.drawRoundedRect(
            {texRect.x + 2.f, texRect.y + 2.f, texRect.width - 4.f, texRect.height * 0.28f},
            nxui::Color(1.f, 1.f, 1.f, 0.065f * m_opacity),
            std::max(2.f, texRadius - 2.f)
        );

        ren.drawRoundedRectOutline(
            texRect.shrunk(1.f),
            nxui::Color(0.78f, 0.84f, 1.f, 0.12f * m_opacity),
            std::max(2.f, texRadius - 1.f),
            1.2f
        );

        ren.drawRoundedRectOutline(
            texRect.shrunk(6.f),
            nxui::Color(0.28f, 0.30f, 0.48f, 0.18f * m_opacity),
            std::max(2.f, texRadius - 6.f),
            1.0f
        );

        return;
    }

    nxui::Color iconTint =
        nxui::Color::white().withAlpha(m_opacity);

    if (m_notLaunchable) {
        iconTint.r = 0.80f;
        iconTint.g = 0.80f;
        iconTint.b = 0.80f;
    }

    ren.drawTextureRounded(
        m_tex,
        texRect,
        texRadius,
        iconTint
    );

    // Global glass reflections, visible on all icons.
    ren.drawRoundedRect(
        {texRect.x + 2.f, texRect.y + 2.f, texRect.width - 4.f, texRect.height * 0.20f},
        nxui::Color(1.f, 1.f, 1.f, 0.10f * m_opacity),
        texRadius
    );

    ren.drawRoundedRect(
        {texRect.x + 10.f, texRect.y + 12.f, texRect.width * 0.46f, texRect.height * 0.08f},
        nxui::Color(1.f, 1.f, 1.f, 0.12f * m_opacity),
        std::max(2.f, texRadius - 6.f)
    );

    // V6: selected icon gets the gradient integrated into its own border
    // and surface, instead of relying on a large coloured cursor outside.
    const float focus = m_focusGlow.value();

    if (focus > 0.01f && m_focused) {
        const float breathe =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.72f);

        const float phase =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.24f);

        const nxui::Color purple(0.40f, 0.10f, 0.84f, 1.f);
        const nxui::Color fuchsia(0.72f, 0.10f, 0.54f, 1.f);
        const nxui::Color blue(0.08f, 0.56f, 1.00f, 1.f);

        nxui::Color mixA(
            purple.r + (fuchsia.r - purple.r) * phase,
            purple.g + (fuchsia.g - purple.g) * phase,
            purple.b + (fuchsia.b - purple.b) * phase,
            1.f
        );

        nxui::Color mixB(
            fuchsia.r + (blue.r - fuchsia.r) * (1.f - phase),
            fuchsia.g + (blue.g - fuchsia.g) * (1.f - phase),
            fuchsia.b + (blue.b - fuchsia.b) * (1.f - phase),
            1.f
        );

        // Integrated coloured border.
        ren.drawRoundedRectOutline(
            texRect.shrunk(1.0f),
            mixA.withAlpha((0.70f + 0.12f * breathe) * focus * m_opacity),
            std::max(2.f, texRadius - 1.f),
            3.2f
        );

        ren.drawRoundedRectOutline(
            texRect.shrunk(4.0f),
            mixB.withAlpha((0.54f + 0.10f * breathe) * focus * m_opacity),
            std::max(2.f, texRadius - 4.f),
            2.4f
        );

        // Soft colour in the glass itself, but very light in the centre.
        ren.drawRoundedRect(
            texRect,
            mixA.withAlpha((0.018f + 0.010f * breathe) * focus * m_opacity),
            texRadius
        );

        ren.drawRoundedRect(
            {texRect.x, texRect.y, texRect.width * 0.30f, texRect.height},
            purple.withAlpha((0.038f + 0.010f * breathe) * focus * m_opacity),
            texRadius
        );

        ren.drawRoundedRect(
            {texRect.right() - texRect.width * 0.30f, texRect.y,
             texRect.width * 0.30f, texRect.height},
            blue.withAlpha((0.038f + 0.010f * breathe) * focus * m_opacity),
            texRadius
        );

        // Stronger glossy reflection on the selected cover.
        ren.drawRoundedRect(
            {texRect.x + 4.f, texRect.y + 4.f, texRect.width - 8.f, texRect.height * 0.17f},
            nxui::Color(1.f, 1.f, 1.f, (0.17f + 0.03f * breathe) * focus * m_opacity),
            texRadius
        );

        ren.drawRoundedRect(
            {texRect.x + 14.f, texRect.y + 12.f, texRect.width * 0.38f, texRect.height * 0.08f},
            nxui::Color(1.f, 1.f, 1.f, (0.22f + 0.04f * breathe) * focus * m_opacity),
            std::max(2.f, texRadius - 6.f)
        );
    }
}
