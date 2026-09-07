#include "AppletButton.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include "HomeControlFocus.hpp"

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
    nxui::Rect iconRect{ix, iy, iconSz, iconSz};

    const float focus = m_selectionHaloEnabled ? m_focusAmount.value() : 0.f;
    iconRect = iconRect.expanded(iconSz * .018f * focus);
    switchu::homeui::drawControlFocus(ren, iconRect, focus, m_opacity);

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
