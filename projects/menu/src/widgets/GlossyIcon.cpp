#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Font.hpp>
#include <cmath>

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

    if (focusGlow > 0.01f && s > 0.5f) {
        // V5: the large coloured halo is now drawn by SelectionCursor.
        // Keep only a very faint ambient bloom behind the selected cover.
        const float breathe =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.85f);

        ren.drawRoundedRect(
            r.expanded(5.f + breathe),
            nxui::Color(
                0.40f,
                0.10f,
                0.78f,
                (0.012f + 0.010f * breathe) *
                    focusGlow * a
            ),
            rad + 6.f
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
        // Application toujours ouverte en arrière-plan :
        // plus de contour vert ni de bouton Play.
        // Un voile bleu/cyan très léger respire directement
        // à l'intérieur de la jaquette, dans l'esprit de la 3DS.
        const float breathe =
            0.5f +
            0.5f *
            std::sin(m_suspendPulse * 0.62f);

        const float suspendedInset =
            8.f * s;

        nxui::Rect activeRect =
            r.shrunk(suspendedInset);

        const float activeRadius =
            std::max(
                2.f,
                rad - 3.f
            );

        // Teinte principale, toujours discrète pour laisser
        // la jaquette parfaitement reconnaissable.
        ren.drawRoundedRect(
            activeRect,
            nxui::Color(
                0.06f,
                0.28f,
                0.66f,
                (0.075f +
                 0.050f * breathe) * a
            ),
            activeRadius
        );

        // Lumière cyan interne qui augmente et diminue lentement.
        ren.drawRoundedRect(
            activeRect.shrunk(4.f * s),
            nxui::Color(
                0.16f,
                0.58f,
                0.96f,
                (0.030f +
                 0.035f * breathe) * a
            ),
            std::max(
                2.f,
                activeRadius - 3.f * s
            )
        );

        // Reflet doux dans la partie haute de la jaquette.
        nxui::Rect upperLight = {
            activeRect.x + 5.f * s,
            activeRect.y + 4.f * s,
            activeRect.width - 10.f * s,
            activeRect.height * 0.38f
        };

        ren.drawRoundedRect(
            upperLight,
            nxui::Color(
                0.54f,
                0.84f,
                1.00f,
                (0.018f +
                 0.026f * breathe) * a
            ),
            std::max(
                2.f,
                activeRadius - 4.f * s
            )
        );

        // Très légère profondeur sombre en bas.
        nxui::Rect lowerShade = {
            activeRect.x + 5.f * s,
            activeRect.y +
                activeRect.height * 0.62f,
            activeRect.width - 10.f * s,
            activeRect.height * 0.34f
        };

        ren.drawRoundedRect(
            lowerShade,
            nxui::Color(
                0.02f,
                0.10f,
                0.30f,
                (0.016f +
                 0.012f * (1.f - breathe)) * a
            ),
            std::max(
                2.f,
                activeRadius - 4.f * s
            )
        );
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

    // V5: stronger frosted placeholder while an icon is empty or loading.
    // It avoids a flat transparent square without using an aggressive blur.
    if (!m_tex || !m_tex->valid()) {
        const float breathe =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.70f);

        ren.drawRoundedRect(
            texRect,
            nxui::Color(
                0.025f,
                0.020f,
                0.070f,
                0.78f * m_opacity
            ),
            texRadius
        );

        ren.drawRoundedRect(
            texRect.shrunk(5.f),
            nxui::Color(
                0.20f,
                0.12f,
                0.42f,
                (0.070f + 0.025f * breathe) * m_opacity
            ),
            std::max(2.f, texRadius - 4.f)
        );

        ren.drawRoundedRect(
            texRect.shrunk(14.f),
            nxui::Color(
                0.12f,
                0.32f,
                0.58f,
                (0.040f + 0.018f * breathe) * m_opacity
            ),
            std::max(2.f, texRadius - 9.f)
        );

        ren.drawRoundedRectOutline(
            texRect.shrunk(1.f),
            nxui::Color(
                0.70f,
                0.62f,
                1.00f,
                0.16f * m_opacity
            ),
            std::max(2.f, texRadius - 1.f),
            1.4f
        );

        // Soft central light gives the impression of deeper frosted glass.
        const nxui::Vec2 center = {
            texRect.x + texRect.width * 0.5f,
            texRect.y + texRect.height * 0.5f
        };

        ren.drawCircle(
            center,
            texRect.width * (0.13f + 0.01f * breathe),
            nxui::Color(
                0.46f,
                0.30f,
                0.82f,
                (0.055f + 0.020f * breathe) * m_opacity
            ),
            32
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

    // V5: extremely light breathing colour filter on the selected cover.
    // The original artwork remains dominant.
    const float focus = m_focusGlow.value();

    if (focus > 0.01f && m_focused) {
        const float breathe =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.72f);

        const float colourShift =
            0.5f + 0.5f * std::sin(m_suspendPulse * 0.24f);

        const nxui::Color purple(0.42f, 0.12f, 0.78f, 1.f);
        const nxui::Color blue(0.08f, 0.48f, 0.92f, 1.f);

        nxui::Color filter(
            purple.r + (blue.r - purple.r) * colourShift,
            purple.g + (blue.g - purple.g) * colourShift,
            purple.b + (blue.b - purple.b) * colourShift,
            1.f
        );

        const float filterAlpha =
            (0.018f + 0.012f * breathe) *
            focus *
            m_opacity;

        ren.drawRoundedRect(
            texRect,
            filter.withAlpha(filterAlpha),
            texRadius
        );
    }
}
