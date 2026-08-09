#include "TitlePillWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kV9TitleCenterX = 640.f;
constexpr float kV9TitleTopY = 532.f;
constexpr float kV9TitleScale = 1.42f;
constexpr float kV9TitleMaxWidth = 900.f;
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
    const float ty = cr.y + (cr.height - textSz.y) * 0.5f;
    const float reveal = std::clamp(m_textReveal.value(), 0.f, 1.f);

    // A tiny shadow keeps the title readable on both the fade and the solid
    // anthracite area without bringing back a panel behind it.
    ren.pushClipRect(cr);
    ren.drawText(m_text,
                 {tx + 1.2f, ty + 1.6f + (1.f - reveal) * 4.f},
                 m_font,
                 nxui::Color(0.f, 0.f, 0.f, 0.42f * m_opacity * reveal),
                 scale);
    ren.drawText(m_text,
                 {tx, ty + (1.f - reveal) * 4.f},
                 m_font,
                 m_textColor.withAlpha(m_opacity * reveal),
                 scale);
    ren.popClipRect();
}

nxui::Vec2 TitlePillWidget::computeContentSize() const {
    if (!m_font || m_text.empty())
        return {0.f, 0.f};

    const nxui::Vec2 base = m_font->measure(m_text);
    return {
        std::min(kV9TitleMaxWidth, base.x * kV9TitleScale),
        base.y * kV9TitleScale
    };
}
