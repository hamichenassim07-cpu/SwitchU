#include "BatteryWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kBatteryWidth = 52.f;
constexpr float kBatteryHeight = 25.f;
constexpr float kBoltSlotWidth = 28.f;
constexpr float kBatteryBoltGap = 10.f;
constexpr float kTextGap = 7.f;
constexpr float kPercentageScale = 1.00f;

void drawLightningBolt(nxui::Renderer& ren,
                       const nxui::Rect& bolt,
                       const nxui::Color& color) {
    const nxui::Vec2 p0{bolt.x + 12.f, bolt.y + 1.f};
    const nxui::Vec2 p1{bolt.x + 4.f, bolt.y + 15.f};
    const nxui::Vec2 p2{bolt.x + 10.f, bolt.y + 15.f};
    const nxui::Vec2 p3{bolt.x + 7.f, bolt.y + 27.f};
    const nxui::Vec2 p4{bolt.x + 18.f, bolt.y + 11.f};
    const nxui::Vec2 p5{bolt.x + 12.f, bolt.y + 11.f};

    ren.drawTriangle(p0, p1, p5, color);
    ren.drawTriangle(p1, p2, p5, color);
    ren.drawTriangle(p2, p3, p4, color);
    ren.drawTriangle(p2, p4, p5, color);
}

} // namespace

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

    const float fixedBatteryGroup = kBatteryWidth + kBatteryBoltGap + kBoltSlotWidth;
    const float groupW = fixedBatteryGroup + kTextGap + textW;
    const float groupH = std::max(kBatteryHeight, textH);
    const float bx = cr.x + (cr.width - groupW) * 0.5f;
    const float by = cr.y + (cr.height - groupH) * 0.5f +
                     (groupH - kBatteryHeight) * 0.5f;

    const nxui::Rect body = {bx, by, kBatteryWidth, kBatteryHeight};
    const nxui::Color edge = m_textColor.withAlpha(0.92f * op);

    // Same clean battery language as the V6.2 lockscreen.
    ren.drawRoundedRectOutline(body, edge, 6.f, 1.9f);
    ren.drawRoundedRect({body.right() + 2.5f, body.y + 7.f, 5.f, 11.f},
                        edge.withAlpha(0.80f * op), 2.f);

    nxui::Rect fill = body.shrunk(3.5f);
    fill.width *= level;
    if (fill.width > 0.5f) {
        const float pulse = 0.72f + 0.28f *
            (0.5f + 0.5f * std::sin(m_chargeAnim * 5.2f));
        const nxui::Color fillColor = level <= 0.20f
            ? nxui::Color(1.f, 0.26f, 0.24f, 0.96f * op)
            : m_textColor.withAlpha((m_charging ? pulse : 0.96f) * op);
        ren.drawRoundedRect(fill, fillColor,
                            std::min(4.f, fill.width * 0.5f));
    }

    const nxui::Rect bolt = {
        body.right() + kBatteryBoltGap + 2.f,
        body.y - 1.5f,
        21.f,
        28.f
    };
    if (m_charging) {
        const float pulse = 0.5f + 0.5f * std::sin(m_chargeAnim * 5.8f);
        const nxui::Color glow(0.40f, 1.f, 0.70f,
                               (0.16f + 0.08f * pulse) * op);
        ren.drawCircle({bolt.x + bolt.width * 0.5f,
                        bolt.y + bolt.height * 0.5f},
                       17.f, glow, 30);
        drawLightningBolt(ren, bolt,
                          nxui::Color(0.42f, 1.f, 0.68f, op));
    }

    if (m_font) {
        const float tx = bx + fixedBatteryGroup + kTextGap;
        // Slight downward optical alignment with the battery body.
        const float ty = cr.y + (cr.height - textH) * 0.5f + 1.5f;
        const nxui::Color text = m_textColor.withAlpha(op);
        const nxui::Color shadow(0.f, 0.f, 0.f, 0.30f * op);
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
        kBatteryWidth + kBatteryBoltGap + kBoltSlotWidth +
            kTextGap + measured.x * kPercentageScale,
        std::max(kBatteryHeight, measured.y * kPercentageScale)
    };
}
