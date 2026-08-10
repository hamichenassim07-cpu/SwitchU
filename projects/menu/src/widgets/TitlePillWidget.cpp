#include "TitlePillWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kV9TitleCenterX = 640.f;
constexpr float kV9TitleTopY = 522.f;
constexpr float kV9TitleScale = 1.42f;
constexpr float kV9TitleMaxWidth = 900.f;
constexpr float kV102SeparatorWidth = 264.f;
constexpr float kV102SeparatorHeight = 2.2f;
constexpr float kV102TitleToSeparatorGap = 18.f;
constexpr float kV102SeparatorToActionsGap = 21.f;
constexpr float kV102ActionLabelScale = 0.79f;
constexpr float kV102ActionGlyphScale = 0.93f;
constexpr float kV102ActionGap = 8.f;
constexpr float kV102PairGap = 42.f;

std::string utf8CodepointV102(unsigned cp) {
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

float TitlePillWidget::anchoredX(float width, float screenWidth) const {
    constexpr float margin = 10.f;
    const float maxX = std::max(margin, screenWidth - width - margin);
    return std::clamp(m_anchorCenterX - width * 0.5f, margin, maxX);
}

void TitlePillWidget::setAnchor(float centerX, float topY, float screenWidth) {
    (void)centerX;
    (void)topY;

    // All HOME labels share one stable visual anchor. This also prevents the
    // title from wandering while the carousel is dragged with touch.
    m_anchorCenterX = std::clamp(kV9TitleCenterX, 0.f, screenWidth);
    m_rect.y = kV9TitleTopY;

    if (!m_layoutInitialized)
        return;

    const float width = std::max(0.f, m_animW.target());
    m_animX.set(anchoredX(width, screenWidth), 0.18f, nxui::Easing::outCubic);
}

void TitlePillWidget::setText(const std::string& text, float screenWidth) {
    const bool textChanged = (m_text != text);
    const bool wasVisible = isVisible();

    m_hideOnCollapse = false;
    setVisible(true);
    m_text = text;
    sizeToFit();
    m_rect.width = std::min(m_rect.width, kV9TitleMaxWidth);

    const float targetW = m_rect.width;
    const float targetX = anchoredX(targetW, screenWidth);

    if (!m_layoutInitialized) {
        m_layoutInitialized = true;
        m_animX.setImmediate(targetX);
        m_animW.setImmediate(targetW);
    } else {
        if (!wasVisible) {
            const float seedW = std::min(targetW, 54.f);
            m_animW.setImmediate(seedW);
            m_animX.setImmediate(anchoredX(seedW, screenWidth));
        }

        m_animX.set(targetX, 0.20f, nxui::Easing::outCubic);
        m_animW.set(targetW, 0.20f, nxui::Easing::outCubic);

        if (textChanged || !wasVisible) {
            m_textReveal.setImmediate(0.f);
            m_textReveal.set(1.f, 0.18f, nxui::Easing::outCubic);
        }
    }

    m_rect.x = m_animX.value();
    m_rect.width = m_animW.value();
}

void TitlePillWidget::setGameActionsVisible(bool visible) {
    if (m_showGameActions == visible)
        return;

    m_showGameActions = visible;
    if (!m_text.empty())
        sizeToFit();
}

void TitlePillWidget::hideAnimated(float screenWidth) {
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
    if (!m_font || m_text.empty())
        return;

    nxui::Rect cr = contentRect();
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
                 nxui::Color(0.f, 0.f, 0.f, 0.44f * m_opacity * reveal),
                 scale);
    ren.drawText(m_text,
                 {tx, ty + lift},
                 m_font,
                 m_textColor.withAlpha(m_opacity * reveal),
                 scale);
    ren.drawText(m_text,
                 {tx + 0.48f, ty + lift},
                 m_font,
                 m_textColor.withAlpha(0.62f * m_opacity * reveal),
                 scale);
    ren.drawText(m_text,
                 {tx - 0.38f, ty + lift},
                 m_font,
                 m_textColor.withAlpha(0.42f * m_opacity * reveal),
                 scale);

    if (m_showGameActions) {
        const float separatorY = ty + textSz.y + kV102TitleToSeparatorGap;
        const float separatorX = cr.x + (cr.width - kV102SeparatorWidth) * 0.5f;
        ren.drawRoundedRect(
            {separatorX, separatorY, kV102SeparatorWidth, kV102SeparatorHeight},
            nxui::Color(0.94f, 0.97f, 1.00f, 0.30f * m_opacity * reveal),
            1.1f
        );

        auto& i18n = nxui::I18n::instance();
        const std::string launch = i18n.tr("hint.launch", "Lancer");
        const std::string move = i18n.tr("hint.move", "Déplacer");
        const std::string aGlyph = utf8CodepointV102(0xE0E0);
        const std::string yGlyph = utf8CodepointV102(0xE0E3);
        nxui::Font* glyphFont = m_iconFont ? m_iconFont : m_font;

        const nxui::Vec2 launchBase = m_font->measure(launch);
        const nxui::Vec2 moveBase = m_font->measure(move);
        const nxui::Vec2 aBase = glyphFont->measure(aGlyph);
        const nxui::Vec2 yBase = glyphFont->measure(yGlyph);
        const float launchW = launchBase.x * kV102ActionLabelScale;
        const float moveW = moveBase.x * kV102ActionLabelScale;
        const float aW = aBase.x * kV102ActionGlyphScale;
        const float yW = yBase.x * kV102ActionGlyphScale;
        const float firstW = aW + kV102ActionGap + launchW;
        const float secondW = yW + kV102ActionGap + moveW;
        const float rowW = firstW + kV102PairGap + secondW;
        float x = cr.x + (cr.width - rowW) * 0.5f;
        const float rowY = separatorY + kV102SeparatorHeight +
                           kV102SeparatorToActionsGap;

        auto drawAction = [&](const std::string& glyph,
                              const std::string& label,
                              float glyphW,
                              float labelW) {
            const nxui::Vec2 glyphBase = glyphFont->measure(glyph);
            const float glyphH = glyphBase.y * kV102ActionGlyphScale;
            const nxui::Vec2 labelBase = m_font->measure(label);
            const float labelH = labelBase.y * kV102ActionLabelScale;
            const float rowH = std::max(glyphH, labelH);

            // switch_icons.ttf is the same icon font already used by Switch U
            // for Nintendo-style controller hints. No hand-drawn square badge.
            ren.drawText(
                glyph,
                {x, rowY + (rowH - glyphH) * 0.5f},
                glyphFont,
                nxui::Color(0.98f, 1.f, 0.99f, 0.94f * m_opacity * reveal),
                kV102ActionGlyphScale
            );
            ren.drawText(
                label,
                {x + glyphW + kV102ActionGap,
                 rowY + (rowH - labelH) * 0.5f},
                m_font,
                nxui::Color(0.94f, 0.96f, 0.98f, 0.82f * m_opacity * reveal),
                kV102ActionLabelScale
            );
            x += glyphW + kV102ActionGap + labelW;
        };

        drawAction(aGlyph, launch, aW, launchW);
        x += kV102PairGap;
        drawAction(yGlyph, move, yW, moveW);
    }

    ren.popClipRect();
}

nxui::Vec2 TitlePillWidget::computeContentSize() const {
    if (!m_font || m_text.empty())
        return {0.f, 0.f};

    const nxui::Vec2 base = m_font->measure(m_text);
    const float titleH = base.y * kV9TitleScale;
    float height = titleH;

    if (m_showGameActions)
        height += kV102TitleToSeparatorGap + kV102SeparatorHeight +
                  kV102SeparatorToActionsGap + 30.f;

    float width = std::min(kV9TitleMaxWidth, base.x * kV9TitleScale);
    if (m_showGameActions)
        width = std::max(width, 330.f);

    return {width, height};
}
