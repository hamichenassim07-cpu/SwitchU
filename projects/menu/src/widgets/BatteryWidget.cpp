#include "BatteryWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include "HomeLiquidGlassStyle.hpp"
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// V6.5 HOME battery: still larger than stock, but recalibrated so the
// percentage never touches the terminal or the battery body.
constexpr float kBatteryWidth = 50.f;
constexpr float kBatteryHeight = 24.f;
constexpr float kTerminalExtent = 7.f;
constexpr float kTextGap = 7.f;
constexpr float kPercentageScale = 0.98f;

} // namespace


BatteryWidget::BatteryWidget() {
    setCornerRadius(22.f);
    setBaseColor(nxui::Color(0.48f, 0.72f, 0.62f, 0.32f));
    setBorderColor(nxui::Color(0.88f, 1.00f, 0.94f, 0.30f));
    setHighlightColor(nxui::Color(1.f, 1.f, 1.f, 0.15f));
    setPanelOpacity(0.92f);
    setLiquidGlassEnabled(true);
    setLiquidGlassShaderEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
}

void BatteryWidget::onRender(nxui::Renderer& ren) {
    const nxui::LiquidGlassSettings saved = ren.liquidGlassSettings();
    switchu::homeui::applyLiquidGlassV102(ren);
    nxui::GlassWidget::onRender(ren);
    ren.liquidGlassSettings() = saved;
}

void BatteryWidget::setBatteryStatus(uint32_t percentage,
                                     bool charging) {
    percentage = std::min<uint32_t>(percentage, 100u);
    m_level = static_cast<float>(percentage) / 100.f;
    m_charging = charging;
    m_timer = 0.f;
}

void BatteryWidget::onContentUpdate(float dt) {
    m_chargeAnim += dt;
    m_timer += dt;

    if (m_timer < 1.f && m_level >= 0.f)
        return;

    m_timer = 0.f;

    u32 charge = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge))) {
        charge = std::min<u32>(charge, 100u);
        m_level = charge / 100.f;
    }

    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&charger)))
        m_charging = charger != PsmChargerType_Unconnected;
}

void BatteryWidget::onContentRender(nxui::Renderer& ren) {
    const nxui::Rect cr = contentRect();
    const float op = opacity();
    const float level = std::clamp(m_level, 0.f, 1.f);

    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%d%%",
                  static_cast<int>(std::round(level * 100.f)));

    const nxui::Vec2 measured = m_font
        ? m_font->measure(buffer)
        : nxui::Vec2{42.f, 18.f};
    const float textW = measured.x * kPercentageScale;
    const float textH = measured.y * kPercentageScale;

    const float groupW = kBatteryWidth + kTerminalExtent + kTextGap + textW;
    const float groupH = std::max(kBatteryHeight, textH);
    const float bx = cr.x + (cr.width - groupW) * 0.5f;
    const float by = cr.y + (cr.height - groupH) * 0.5f +
                     (groupH - kBatteryHeight) * 0.5f;

    const nxui::Rect body = {bx, by, kBatteryWidth, kBatteryHeight};
    const nxui::Color edge = m_textColor.withAlpha(0.94f * op);

    ren.drawRoundedRectOutline(body, edge, 5.8f, 1.65f);
    ren.drawRoundedRect({body.right() + 2.2f, body.y + 6.5f, 4.5f, 11.f},
                        edge.withAlpha(0.82f * op), 1.8f);

    nxui::Rect fill = body.shrunk(3.4f);
    fill.width *= level;
    if (fill.width > 0.5f) {
        // The charge state is now carried entirely by the fill. It stays
        // visible while its green luminosity pulses, so the real level can
        // always be read.
        const float blink = 0.48f + 0.52f *
            (0.5f + 0.5f * std::sin(m_chargeAnim * 5.3f));
        nxui::Color fillColor;
        if (m_charging) {
            fillColor = nxui::Color(0.22f, 1.00f, 0.48f,
                                    (0.52f + 0.46f * blink) * op);
        } else if (level <= 0.20f) {
            fillColor = nxui::Color(1.f, 0.26f, 0.24f, 0.96f * op);
        } else {
            fillColor = m_textColor.withAlpha(0.96f * op);
        }
        ren.drawRoundedRect(fill, fillColor,
                            std::min(4.0f, fill.width * 0.5f));

        if (m_charging) {
            ren.drawRoundedRect(fill.expanded(1.0f),
                                nxui::Color(0.22f, 1.f, 0.48f,
                                            (0.025f + 0.055f * blink) * op),
                                std::min(4.8f, fill.width * 0.5f));
        }
    }

    if (m_font) {
        const float tx = body.right() + kTerminalExtent + kTextGap;
        const float ty = cr.y + (cr.height - textH) * 0.5f + 1.2f;
        const nxui::Color text = m_textColor.withAlpha(op);
        const nxui::Color shadow(0.f, 0.f, 0.f, 0.28f * op);
        ren.drawText(buffer, {tx + 1.f, ty + 1.f}, m_font,
                     shadow, kPercentageScale);
        ren.drawText(buffer, {tx, ty}, m_font,
                     text, kPercentageScale);
    }
}

nxui::Vec2 BatteryWidget::computeContentSize() const {
    const nxui::Vec2 measured = m_font
        ? m_font->measure("100%")
        : nxui::Vec2{42.f, 18.f};

    return {
        kBatteryWidth + kTerminalExtent + kTextGap + measured.x * kPercentageScale,
        std::max(kBatteryHeight, measured.y * kPercentageScale)
    };
}
