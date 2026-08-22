#include "TitlePillWidget.hpp"
#include "LaunchAnimation.hpp"
#include "../core/PlayTimeProvider.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr float kV9TitleCenterX = 640.f;
constexpr float kV9TitleTopY = 536.f;
constexpr float kV9TitleScale = 1.42f;
constexpr float kV9TitleMaxWidth = 900.f;

constexpr float kActionsSeparatorY = 596.f;
constexpr float kActionsSeparatorWidth = 364.f;
constexpr float kActionsSeparatorHeight = 2.4f;
constexpr float kActionsRowY = 622.f;
constexpr float kActionLabelScale = 0.83f;
constexpr float kActionGlyphScale = 0.97f;
constexpr float kActionGap = 8.f;
constexpr float kActionPairGap = 42.f;

// V10.27: compact play-time row under A Lancer / Y Déplacer.
constexpr float kPlayTimeY = 661.f;
constexpr float kPlayTimeTextScale = 0.70f;
constexpr float kPlayTimeClockRadius = 6.8f;
constexpr float kPlayTimeGap = 8.f;

std::string utf8CodepointTP(unsigned cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

}

TitlePillWidget::TitlePillWidget() {
    // V9: this widget is no longer a glass pill. It becomes the large, quiet
    // title line under the centred cover. Keeping the existing class avoids
    // changing the HOME wiring and all focus callbacks.
    setLiquidGlassShaderEnabled(false);
    setLiquidGlassEnabled(false);
    setForceLiquidGlass(false);
    setBlurEnabled(false);
    setPanelOpacity(0.f);
    setBorderWidth(0.f);
}

void TitlePillWidget::setProfileOriginalMode(bool enabled) {
    if (m_profileOriginalMode == enabled)
        return;

    m_profileOriginalMode = enabled;
    m_layoutInitialized = false;
    m_hideOnCollapse = false;
    m_textReveal.setImmediate(1.f);

    if (enabled) {
        // Exact Switch U master TitlePill material/setup path for profile focus.
        setLiquidGlassShaderEnabled(false);
        setLiquidGlassEnabled(true);
        setForceLiquidGlass(true);
        setBlurEnabled(false);
        setBlurRadius(1.5f);
        setBlurPasses(1);
        setCornerRadius(24.f);
        setBaseColor({0.28f, 0.32f, 0.38f, 0.20f});
        setBorderColor({0.96f, 0.97f, 1.0f, 0.16f});
        setHighlightColor({1.0f, 1.0f, 1.0f, 0.05f});
        setBorderWidth(1.0f);
        setPanelOpacity(1.f);
        setPadding(9.f, 22.f, 9.f, 22.f);
        setPosition(0.f, 630.f);
        m_showGameActions = false;
    } else {
        // Return to the V10.x game-title presentation.
        setLiquidGlassShaderEnabled(false);
        setLiquidGlassEnabled(false);
        setForceLiquidGlass(false);
        setBlurEnabled(false);
        setPanelOpacity(0.f);
        setBorderWidth(0.f);
        setPosition(0.f, kV9TitleTopY);
    }
}

float TitlePillWidget::anchoredX(float width, float screenWidth) const {
    constexpr float margin = 10.f;
    const float maxX = std::max(margin, screenWidth - width - margin);
    return std::clamp(m_anchorCenterX - width * 0.5f, margin, maxX);
}

void TitlePillWidget::setAnchor(float centerX, float topY, float screenWidth) {
    if (m_profileOriginalMode) {
        (void)centerX;
        (void)topY;
        (void)screenWidth;
        return;
    }

    (void)centerX;
    (void)topY;

    // V10.27: the game-title container is a fixed screen-space region.
    // Carousel motion can change only the string, never this geometry.
    m_anchorCenterX = std::clamp(kV9TitleCenterX, 0.f, screenWidth);
    m_rect.y = kV9TitleTopY;

    const float fixedWidth = std::min(kV9TitleMaxWidth, screenWidth - 20.f);
    const float fixedX = std::clamp(
        kV9TitleCenterX - fixedWidth * 0.5f,
        10.f,
        std::max(10.f, screenWidth - fixedWidth - 10.f));

    m_rect.x = fixedX;
    m_rect.width = fixedWidth;
    if (m_layoutInitialized) {
        m_animX.setImmediate(fixedX);
        m_animW.setImmediate(fixedWidth);
    }
}

void TitlePillWidget::setText(const std::string& text, float screenWidth) {
    if (m_profileOriginalMode) {
        // Exact logic copied from Switch U master TitlePillWidget::setText.
        if (m_text == text && m_layoutInitialized)
            return;

        const bool wasVisible = isVisible();
        m_hideOnCollapse = false;
        setVisible(true);
        m_text = text;
        sizeToFit();
        setCornerRadius(m_rect.height * 0.5f);
        float targetW = m_rect.width;
        float targetX = (screenWidth - targetW) * 0.5f;

        if (!m_layoutInitialized) {
            m_layoutInitialized = true;
            m_animX.setImmediate(targetX);
            m_animW.setImmediate(targetW);
        } else {
            if (!wasVisible) {
                float seedW = std::min(targetW, 54.f);
                m_animW.setImmediate(seedW);
                m_animX.setImmediate((screenWidth - seedW) * 0.5f);
            }
            m_animX.set(targetX, 0.22f, nxui::Easing::outCubic);
            m_animW.set(targetW, 0.22f, nxui::Easing::outCubic);
            m_textReveal.setImmediate(0.32f);
            m_textReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
        }

        m_rect.x = m_animX.value();
        m_rect.width = m_animW.value();
        return;
    }
    const bool textChanged = (m_text != text);
    const bool wasVisible = isVisible();

    m_hideOnCollapse = false;
    setVisible(true);
    m_text = text;
    sizeToFit();

    // V10.27: never animate the game-title container horizontally when the
    // selected game changes. The text may fade in, but its anchor stays fixed.
    const float fixedWidth = std::min(kV9TitleMaxWidth, screenWidth - 20.f);
    const float fixedX = std::clamp(
        kV9TitleCenterX - fixedWidth * 0.5f,
        10.f,
        std::max(10.f, screenWidth - fixedWidth - 10.f));

    m_rect.x = fixedX;
    m_rect.width = fixedWidth;
    m_animX.setImmediate(fixedX);
    m_animW.setImmediate(fixedWidth);
    m_layoutInitialized = true;

    if (textChanged || !wasVisible) {
        m_textReveal.setImmediate(0.f);
        m_textReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
    }
}

void TitlePillWidget::setSelectedTitleId(std::uint64_t titleId) {
    // Re-query through the provider cache even when focus returns to the same
    // title, so playtime refreshes after a real gameplay session.
    m_selectedTitleId = titleId;
    m_playTimeMinutes = 0;
    m_playTimeAvailable = false;

    if (titleId == 0)
        return;

    std::uint64_t minutes = 0;
    if (PlayTimeProvider::instance().minutesFor(titleId, minutes)) {
        m_playTimeMinutes = minutes;
        m_playTimeAvailable = true;
    }
}

void TitlePillWidget::setGameActionsVisible(bool visible) {
    // V10.9 compatibility only. The real separator/A-Y strip now lives in
    // GameActionsHudWidget at fixed absolute coordinates.
    m_showGameActions = visible;
}

void TitlePillWidget::hideAnimated(float screenWidth) {
    if (m_profileOriginalMode) {
        // Exact logic copied from Switch U master TitlePillWidget::hideAnimated.
        if (!isVisible() && !m_hideOnCollapse)
            return;

        if (!m_layoutInitialized) {
            setVisible(false);
            return;
        }

        const float seedW = std::max(0.f, m_animW.value());
        m_animW.set(seedW, 0.01f, nxui::Easing::outCubic);
        m_animX.set((screenWidth - seedW) * 0.5f, 0.01f, nxui::Easing::outCubic);
        m_animW.set(0.f, 0.18f, nxui::Easing::outCubic);
        m_animX.set(screenWidth * 0.5f, 0.18f, nxui::Easing::outCubic);
        m_textReveal.set(0.f, 0.12f, nxui::Easing::outCubic);
        m_hideOnCollapse = true;
        setVisible(true);
        return;
    }
    if (!isVisible() && !m_hideOnCollapse)
        return;

    if (!m_layoutInitialized) {
        setVisible(false);
        return;
    }

    const float seedW = std::max(0.f, m_animW.value());
    m_animW.set(seedW, 0.01f, nxui::Easing::outCubic);
    m_animX.set(anchoredX(seedW, screenWidth), 0.01f, nxui::Easing::outCubic);
    m_animW.set(0.f, 0.18f, nxui::Easing::outCubic);
    m_animX.set(m_anchorCenterX, 0.18f, nxui::Easing::outCubic);
    m_textReveal.set(0.f, 0.12f, nxui::Easing::outCubic);
    m_hideOnCollapse = true;
    setVisible(true);
}

void TitlePillWidget::onContentUpdate(float dt) {
    (void)dt;

    if (m_layoutInitialized) {
        m_rect.x = m_animX.value();
        m_rect.width = m_animW.value();

        if (m_hideOnCollapse && m_animW.value() <= 0.5f) {
            m_hideOnCollapse = false;
            m_text.clear();
            setVisible(false);
        }
    }
}

void TitlePillWidget::onContentRender(nxui::Renderer& ren) {
    if (m_profileOriginalMode) {
        // Exact rendering path from Switch U master TitlePillWidget.
        if (!m_font || m_text.empty()) return;

        nxui::Rect cr = contentRect();
        nxui::Vec2 textSz = m_font->measure(m_text);
        float tx = cr.x + (cr.width  - textSz.x) * 0.5f;
        float ty = cr.y + (cr.height - textSz.y) * 0.5f;
        float reveal = std::clamp(m_textReveal.value(), 0.f, 1.f);
        ren.pushClipRect(cr);
        ren.drawText(m_text, {tx, ty + (1.f - reveal) * 3.f}, m_font,
                     m_textColor.withAlpha(m_opacity * reveal), 1.f);
        ren.popClipRect();
        return;
    }
    if (!m_font || m_text.empty())
        return;

    // V10.20: the lower HOME information physically clears the insertion
    // path. It starts late in the 3D spin and is fully off-screen before the
    // game card begins descending.
    const float launchExitRaw =
        std::clamp(LaunchAnimation::globalHudExitProgress(), 0.f, 1.f);
    const float launchExit =
        launchExitRaw * launchExitRaw * (3.f - 2.f * launchExitRaw);
    const float launchAlpha = 1.f - launchExit;
    const float launchYOffset = 214.f * launchExit;
    if (launchAlpha <= 0.002f)
        return;

    nxui::Rect cr = contentRect();
    cr.y += launchYOffset;
    const nxui::Vec2 base = m_font->measure(m_text);
    if (base.x <= 0.f || base.y <= 0.f)
        return;

    float scale = kV9TitleScale;
    if (base.x * scale > cr.width && cr.width > 1.f)
        scale = std::max(0.92f, cr.width / base.x);

    const nxui::Vec2 textSz = {base.x * scale, base.y * scale};
    const float tx = cr.x + (cr.width - textSz.x) * 0.5f;
    const float ty = cr.y;
    const float reveal = std::clamp(m_textReveal.value(), 0.f, 1.f);
    const float lift = (1.f - reveal) * 4.f;

    ren.pushClipRect(cr);

    // V10.3: keep the V9 size, but make ONLY the carousel title visually
    // heavier. Sub-pixel duplicate passes emulate a semibold face without
    // enlarging the text or changing the rest of the HOME typography.
    ren.drawText(m_text,
                 {tx + 1.2f, ty + 1.6f + lift},
                 m_font,
                 nxui::Color(0.f, 0.f, 0.f, 0.44f * m_opacity * reveal * launchAlpha),
                 scale);
    ren.drawText(m_text,
                 {tx, ty + lift},
                 m_font,
                 m_textColor.withAlpha(m_opacity * reveal * launchAlpha),
                 scale);
    ren.drawText(m_text,
                 {tx + 0.48f, ty + lift},
                 m_font,
                 m_textColor.withAlpha(0.62f * m_opacity * reveal * launchAlpha),
                 scale);
    ren.drawText(m_text,
                 {tx - 0.38f, ty + lift},
                 m_font,
                 m_textColor.withAlpha(0.42f * m_opacity * reveal * launchAlpha),
                 scale);


    ren.popClipRect();

    if (m_showGameActions && m_font) {
        ren.drawRoundedRect(
            {kV9TitleCenterX - kActionsSeparatorWidth * 0.5f,
             kActionsSeparatorY + launchYOffset,
             kActionsSeparatorWidth,
             kActionsSeparatorHeight},
            nxui::Color(0.94f, 0.97f, 1.00f, 0.30f * m_opacity * launchAlpha),
            1.1f
        );

        auto& i18n = nxui::I18n::instance();
        const std::string launch = i18n.tr("hint.launch", "Lancer");
        const std::string move = i18n.tr("hint.move", "Déplacer");
        const std::string aGlyph = utf8CodepointTP(0xE0E0);
        const std::string yGlyph = utf8CodepointTP(0xE0E3);
        nxui::Font* glyphFont = m_iconFont ? m_iconFont : m_font;

        const nxui::Vec2 launchBase = m_font->measure(launch);
        const nxui::Vec2 moveBase = m_font->measure(move);
        const nxui::Vec2 aBase = glyphFont->measure(aGlyph);
        const nxui::Vec2 yBase = glyphFont->measure(yGlyph);

        const float launchW = launchBase.x * kActionLabelScale;
        const float moveW = moveBase.x * kActionLabelScale;
        const float aW = aBase.x * kActionGlyphScale;
        const float yW = yBase.x * kActionGlyphScale;
        const float firstW = aW + kActionGap + launchW;
        const float secondW = yW + kActionGap + moveW;
        const float rowW = firstW + kActionPairGap + secondW;
        float x = kV9TitleCenterX - rowW * 0.5f;

        auto drawAction = [&](const std::string& glyph,
                              const std::string& label,
                              float glyphW,
                              float labelW) {
            const nxui::Vec2 glyphBase = glyphFont->measure(glyph);
            const nxui::Vec2 labelBase = m_font->measure(label);
            const float glyphH = glyphBase.y * kActionGlyphScale;
            const float labelH = labelBase.y * kActionLabelScale;
            const float rowH = std::max(glyphH, labelH);

            ren.drawText(
                glyph,
                {x, kActionsRowY + launchYOffset + (rowH - glyphH) * 0.5f},
                glyphFont,
                nxui::Color(0.98f, 1.f, 0.99f, 0.98f * m_opacity * launchAlpha),
                kActionGlyphScale
            );
            ren.drawText(
                label,
                {x + glyphW + kActionGap,
                 kActionsRowY + launchYOffset + (rowH - labelH) * 0.5f},
                m_font,
                nxui::Color(0.94f, 0.96f, 0.98f, 0.92f * m_opacity * launchAlpha),
                kActionLabelScale
            );
            x += glyphW + kActionGap + labelW;
        };

        drawAction(aGlyph, launch, aW, launchW);
        x += kActionPairGap;
        drawAction(yGlyph, move, yW, moveW);

        // V10.27: real Horizon play time, with no label and no colon. A small
        // vector clock is used instead of an emoji so rendering is font-safe.
        if (m_playTimeAvailable) {
            char duration[48] = {};
            const unsigned long long totalMinutes =
                static_cast<unsigned long long>(m_playTimeMinutes);
            if (m_playTimeMinutes < 60) {
                std::snprintf(duration, sizeof(duration), "%llu min", totalMinutes);
            } else {
                const unsigned long long hours = totalMinutes / 60ULL;
                const unsigned long long minutes = totalMinutes % 60ULL;
                std::snprintf(duration, sizeof(duration),
                              "%llu h %02llu min", hours, minutes);
            }

            const nxui::Vec2 timeBase = m_font->measure(duration);
            const float textW = timeBase.x * kPlayTimeTextScale;
            const float clockDiameter = kPlayTimeClockRadius * 2.f;
            const float groupW = clockDiameter + kPlayTimeGap + textW;
            const float startX = kV9TitleCenterX - groupW * 0.5f;
            const float cy = kPlayTimeY + launchYOffset + kPlayTimeClockRadius;
            const nxui::Color timeColor(0.92f, 0.95f, 0.98f,
                                        0.72f * m_opacity * launchAlpha);

            const nxui::Vec2 clockCenter{
                startX + kPlayTimeClockRadius,
                cy
            };
            // drawCircle has no outline mode; approximate a clean clock ring
            // with short line segments to keep the icon minimal.
            constexpr int segments = 28;
            nxui::Vec2 previous{
                clockCenter.x + kPlayTimeClockRadius, clockCenter.y
            };
            for (int i = 1; i <= segments; ++i) {
                const float a = 6.2831853071795864769f *
                    (static_cast<float>(i) / static_cast<float>(segments));
                const nxui::Vec2 current{
                    clockCenter.x + std::cos(a) * kPlayTimeClockRadius,
                    clockCenter.y + std::sin(a) * kPlayTimeClockRadius
                };
                ren.drawLine(previous, current, timeColor, 1.35f);
                previous = current;
            }
            ren.drawLine(clockCenter,
                         {clockCenter.x, clockCenter.y - 3.7f},
                         timeColor, 1.45f);
            ren.drawLine(clockCenter,
                         {clockCenter.x + 3.0f, clockCenter.y + 1.8f},
                         timeColor, 1.45f);

            const float textH = timeBase.y * kPlayTimeTextScale;
            ren.drawText(
                duration,
                {startX + clockDiameter + kPlayTimeGap,
                 cy - textH * 0.5f},
                m_font,
                timeColor,
                kPlayTimeTextScale
            );
        }
    }
}

nxui::Vec2 TitlePillWidget::computeContentSize() const {
    if (m_profileOriginalMode) {
        // Exact Switch U master sizing path.
        if (!m_font || m_text.empty()) return {0.f, 0.f};
        return m_font->measure(m_text);
    }
    if (!m_font || m_text.empty())
        return {0.f, 0.f};

    const nxui::Vec2 base = m_font->measure(m_text);
    const float titleH = base.y * kV9TitleScale;
    const float width = std::min(kV9TitleMaxWidth, base.x * kV9TitleScale);
    return {width, titleH};
}
