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
    setBaseColor(nxui::Color(0.70f, 0.84f, 0.98f, 0.34f));
    setBorderColor(nxui::Color(0.96f, 0.99f, 1.00f, 0.50f));
    setHighlightColor(nxui::Color(1.f, 1.f, 1.f, 0.22f));
    // V10.6: battery/Wi-Fi are text/icon-only, with no Liquid Glass panel.
    setPanelOpacity(0.f);
    setLiquidGlassEnabled(false);
    setLiquidGlassShaderEnabled(false);
    setForceLiquidGlass(false);
    setBorderWidth(0.f);
    setBlurEnabled(false);

    m_nifmReady = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
}

BatteryWidget::~BatteryWidget() {
    if (m_nifmReady)
        nifmExit();
}

void BatteryWidget::onRender(nxui::Renderer& ren) {
    nxui::GlassWidget::onRender(ren);
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
    m_wifiTimer += dt;

    if (m_wifiTimer >= 1.f) {
        m_wifiTimer = 0.f;
        m_wifiRadioEnabled = false;
        m_wifiConnected = false;
        m_wifiStrength = 0;

        if (m_nifmReady) {
            bool wirelessEnabled = false;
            if (R_SUCCEEDED(nifmIsWirelessCommunicationEnabled(&wirelessEnabled)))
                m_wifiRadioEnabled = wirelessEnabled;

            NifmInternetConnectionType type = NifmInternetConnectionType_WiFi;
            NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
            u32 strength = 0;
            if (m_wifiRadioEnabled &&
                R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
                type == NifmInternetConnectionType_WiFi &&
                status == NifmInternetConnectionStatus_Connected) {
                m_wifiConnected = true;
                m_wifiStrength = std::min<u32>(strength, 3u);
            }
        }
    }

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

    // Real NIFM Wi-Fi indicator. It is deliberately drawn immediately to the
    // left of the battery widget without a background capsule.
    const nxui::Color wifiOn = m_textColor.withAlpha(0.95f * op);
    const nxui::Color wifiOff = m_textColor.withAlpha(0.18f * op);
    const float wifiX = cr.x - 45.f;
    const float wifiY = cr.y + cr.height * 0.5f + 8.f;

    auto drawWifiArc = [&](float radius, bool active, float thickness) {
        constexpr int segments = 18;
        constexpr float startA = 3.78f;
        constexpr float endA = 5.64f;
        nxui::Vec2 prev{
            wifiX + std::cos(startA) * radius,
            wifiY + std::sin(startA) * radius
        };
        for (int i = 1; i <= segments; ++i) {
            const float a = startA + (endA - startA) *
                (static_cast<float>(i) / static_cast<float>(segments));
            nxui::Vec2 cur{
                wifiX + std::cos(a) * radius,
                wifiY + std::sin(a) * radius
            };
            ren.drawLine(prev, cur, active ? wifiOn : wifiOff, thickness);
            prev = cur;
        }
    };

    // 0..3 bars returned directly by Horizon/NIFM.
    const bool bar1 = m_wifiConnected && m_wifiStrength >= 1u;
    const bool bar2 = m_wifiConnected && m_wifiStrength >= 2u;
    const bool bar3 = m_wifiConnected && m_wifiStrength >= 3u;
    ren.drawCircle({wifiX, wifiY - 1.f}, 3.2f,
                   m_wifiConnected ? wifiOn : wifiOff, 14);
    drawWifiArc(10.0f, bar1, 2.6f);
    drawWifiArc(17.0f, bar2, 2.6f);
    drawWifiArc(24.0f, bar3, 2.6f);

    if (!m_wifiRadioEnabled) {
        ren.drawLine({wifiX - 16.f, wifiY - 23.f},
                     {wifiX + 17.f, wifiY + 3.f},
                     m_textColor.withAlpha(0.72f * op), 2.7f);
    }

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
