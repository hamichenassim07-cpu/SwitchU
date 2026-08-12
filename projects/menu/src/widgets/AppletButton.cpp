#include "AppletButton.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>

AppletButton::AppletButton() {
    setCornerRadius(16.f);
    setPadding(6.f);
    setLiquidGlassEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setBorderWidth(2.2f);
    m_i18nListenerId = nxui::I18n::instance().addLanguageChangedListener([this]() {
        refreshLocalizedLabel();
    });
}

AppletButton::~AppletButton() {
    nxui::I18n::instance().removeLanguageChangedListener(m_i18nListenerId);
}

void AppletButton::setChromeEnabled(bool enabled) {
    m_chromeEnabled = enabled;
    setPadding(enabled ? 6.f : 0.f);
    setLiquidGlassEnabled(enabled);
    setForceLiquidGlass(enabled);
    setBlurEnabled(false);
    setPanelOpacity(enabled ? 1.f : 0.f);
    setBorderWidth(enabled ? 2.2f : 0.f);
}

void AppletButton::setLabel(const std::string& l) {
    m_useLabelKey = false;
    m_labelSource = l;
    refreshLocalizedLabel();
}

void AppletButton::setLabelKey(const std::string& key, const std::string& fallback) {
    m_useLabelKey = true;
    m_labelKey = key;
    m_labelFallback = fallback.empty() ? key : fallback;
    refreshLocalizedLabel();
}

void AppletButton::refreshLocalizedLabel() {
    if (m_useLabelKey)
        m_label = nxui::I18n::instance().tr(m_labelKey, m_labelFallback);
    else
        m_label = nxui::I18n::instance().tr(m_labelSource, m_labelSource);
    setAccessibilityLabel(m_label);
}

void AppletButton::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = m_chromeEnabled ? contentRect() : rect();
    const float iconSz = std::min(cr.width, cr.height);
    const float ix = cr.x + (cr.width - iconSz) * 0.5f;
    const float iy = cr.y + (cr.height - iconSz) * 0.5f;
    const nxui::Rect iconRect{ix, iy, iconSz, iconSz};

    // V10.12.1: draw selection inside the actual button render path. This
    // cannot be lost behind a Box/content layer and never moves between items.
    if (m_selectionHaloEnabled && m_focused) {
        const float haloSide = iconSz + 12.f;
        const float cx = ix + iconSz * 0.5f;
        const float cy = iy + iconSz * 0.5f;
        const nxui::Rect haloRect{
            cx - haloSide * 0.5f,
            cy - haloSide * 0.5f,
            haloSide,
            haloSide
        };
        const float radius = haloSide * 0.5f;
        ren.drawRoundedRect(
            haloRect.expanded(2.f),
            nxui::Color(1.f, 1.f, 1.f, 0.10f * m_opacity),
            radius + 2.f
        );
        ren.drawRoundedRectOutline(
            haloRect.expanded(3.5f),
            nxui::Color(1.f, 1.f, 1.f, 0.28f * m_opacity),
            radius + 3.5f,
            6.f
        );
        ren.drawRoundedRectOutline(
            haloRect,
            nxui::Color(1.f, 1.f, 1.f, 0.98f * m_opacity),
            radius,
            2.6f
        );
    }

    if (!m_icon || !m_icon->valid())
        return;

    const float iconCorner = m_iconCircular
        ? iconSz * 0.5f
        : std::max(0.f, cornerRadius() - (m_chromeEnabled ? 4.f : 0.f));

    ren.drawTextureRounded(
        m_icon,
        iconRect,
        iconCorner,
        nxui::Color::white().withAlpha(m_opacity)
    );
}
