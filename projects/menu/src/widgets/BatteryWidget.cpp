#include "BatteryWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kBatteryWidth = 72.f;
constexpr float kBatteryHeight = 30.f;
constexpr float kPercentageScale = 1.08f;
}

void BatteryWidget::setBatteryStatus(uint32_t percentage,
                                     bool charging) {
    if (percentage > 100)
        percentage = 100;

    m_level =
        static_cast<float>(percentage) / 100.f;

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

    if (R_SUCCEEDED(
            psmGetBatteryChargePercentage(&charge))) {
        if (charge > 100)
            charge = 100;

        m_level = charge / 100.f;
    }

    PsmChargerType ct =
        PsmChargerType_Unconnected;

    if (R_SUCCEEDED(psmGetChargerType(&ct)))
        m_charging =
            (ct != PsmChargerType_Unconnected);
}

void BatteryWidget::onContentRender(nxui::Renderer& ren) {
    nxui::Rect cr = contentRect();
    const float op = m_opacity;

    nxui::Rect glassRect = m_rect.shrunk(1.8f);
    const float glassRadius = cornerRadius();

    // V6.1: stronger visible glass contours.
    // Battery geometry and percentage text remain exactly as in V6.
    ren.drawRoundedRectOutline(
        glassRect.expanded(1.2f),
        nxui::Color(
            0.76f,
            0.88f,
            1.00f,
            0.20f * op
        ),
        glassRadius + 1.2f,
        4.2f
    );

    ren.drawRoundedRectOutline(
        glassRect,
        nxui::Color(
            0.94f,
            0.98f,
            1.00f,
            0.58f * op
        ),
        glassRadius,
        2.5f
    );

    ren.drawRoundedRectOutline(
        glassRect.shrunk(2.8f),
        nxui::Color(
            1.00f,
            1.00f,
            1.00f,
            0.24f * op
        ),
        std::max(2.f, glassRadius - 2.8f),
        1.15f
    );

    ren.drawRoundedRect(
        {
            glassRect.x + 8.f,
            glassRect.y + 5.f,
            glassRect.width - 16.f,
            4.0f
        },
        nxui::Color(
            1.00f,
            1.00f,
            1.00f,
            0.34f * op
        ),
        2.f
    );

    ren.drawRoundedRect(
        {
            glassRect.x + 12.f,
            glassRect.bottom() - 7.f,
            glassRect.width - 24.f,
            2.8f
        },
        nxui::Color(
            0.50f,
            0.72f,
            1.00f,
            0.16f * op
        ),
        1.4f
    );

    float level = std::clamp(m_level, 0.f, 1.f);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(level * 100));

    nxui::Vec2 textBase =
        m_font ? m_font->measure(buf) : nxui::Vec2{42.f, 18.f};

    const float textW = textBase.x * kPercentageScale;
    const float textH = textBase.y * kPercentageScale;

    const float iconW = kBatteryWidth;
    const float iconH = kBatteryHeight;
    const float gap = 14.f;
    const float groupW = iconW + gap + textW;
    const float groupH = std::max(iconH, textH);

    const float startX = cr.x + (cr.width - groupW) * 0.5f;
    const float startY = cr.y + (cr.height - groupH) * 0.5f;

    nxui::Rect body = {
        startX,
        startY + (groupH - iconH) * 0.5f,
        iconW,
        iconH
    };

    // Stronger glossy glass accents for the whole battery widget.
    ren.drawRoundedRect(
        {cr.x + 6.f, cr.y + 4.f, cr.width - 12.f, cr.height * 0.34f},
        nxui::Color(1.f, 1.f, 1.f, 0.120f * op),
        18.f
    );

    ren.drawRoundedRect(
        {cr.x + 12.f, cr.y + 10.f, cr.width * 0.48f, cr.height * 0.14f},
        nxui::Color(1.f, 1.f, 1.f, 0.095f * op),
        14.f
    );

    const float radius = body.height * 0.43f;

    ren.drawRoundedRect(
        body,
        nxui::Color(0.94f, 0.97f, 1.f, 0.10f * op),
        radius
    );

    ren.drawRoundedRect(
        {body.x + 2.f, body.y + 2.f, body.width - 4.f, body.height * 0.42f},
        nxui::Color(1.f, 1.f, 1.f, 0.16f * op),
        radius * 0.78f
    );

    ren.drawRoundedRectOutline(
        body,
        nxui::Color(0.90f, 0.96f, 1.f, 0.74f * op),
        radius,
        1.7f
    );

    ren.drawRoundedRect(
        {body.right() + 2.4f, body.y + body.height * 0.30f, 5.8f, body.height * 0.40f},
        nxui::Color(0.92f, 0.97f, 1.f, 0.55f * op),
        2.9f
    );

    nxui::Rect inner = body.shrunk(4.f);
    float fillW = std::max(0.f, inner.width * level);

    nxui::Color fill =
        (level > 0.20f)
            ? nxui::Color(0.34f, 0.94f, 0.56f, op)
            : nxui::Color(0.96f, 0.25f, 0.22f, op);

    if (m_charging && level > 0.20f) {
        const float pulse =
            0.78f + 0.22f *
            (0.5f + 0.5f * std::sin(m_chargeAnim * 4.6f));

        fill = nxui::Color(0.48f, 0.98f, 0.70f, op * pulse);
    }

    if (fillW > 0.5f) {
        nxui::Rect fillRect = {
            inner.x,
            inner.y,
            fillW,
            inner.height
        };

        ren.drawRoundedRect(
            fillRect,
            fill,
            std::min(radius * 0.66f, fillRect.width * 0.5f)
        );

        ren.drawRoundedRect(
            {fillRect.x + 1.5f, fillRect.y + 1.2f,
             std::max(0.f, fillRect.width - 3.f),
             fillRect.height * 0.34f},
            nxui::Color(1.f, 1.f, 1.f, 0.22f * op),
            std::min(radius * 0.44f, fillRect.width * 0.45f)
        );
    }

    if (m_charging) {
        nxui::Rect bolt = {
            body.x + body.width * 0.36f,
            body.y + body.height * 0.18f,
            body.height * 0.52f,
            body.height * 0.64f
        };

        const float pulse =
            0.72f + 0.28f *
            (0.5f + 0.5f * std::sin(m_chargeAnim * 5.4f));

        nxui::Vec2 p0{bolt.x + bolt.width * 0.34f, bolt.y};
        nxui::Vec2 p1{bolt.x + bolt.width * 0.70f, bolt.y};
        nxui::Vec2 p2{bolt.x + bolt.width * 0.48f, bolt.y + bolt.height * 0.42f};
        nxui::Vec2 p3{bolt.x + bolt.width * 0.75f, bolt.y + bolt.height * 0.42f};
        nxui::Vec2 p4{bolt.x + bolt.width * 0.28f, bolt.y + bolt.height};
        nxui::Vec2 p5{bolt.x + bolt.width * 0.44f, bolt.y + bolt.height * 0.56f};
        nxui::Vec2 p6{bolt.x + bolt.width * 0.18f, bolt.y + bolt.height * 0.56f};

        nxui::Color boltFill(1.f, 0.88f, 0.24f, 0.92f * op * pulse);

        ren.drawTriangle(p0, p1, p2, boltFill);
        ren.drawTriangle(p2, p3, p4, boltFill);
        ren.drawTriangle(p4, p5, p6, boltFill);
    }

    if (m_font) {
        const float tx = body.right() + gap;
        const float ty = cr.y + (cr.height - textH) * 0.5f;

        nxui::Color shadow(0.f, 0.f, 0.f, 0.34f * op);
        nxui::Color text = m_textColor.withAlpha(op);

        ren.drawText(buf, {tx + 1.f, ty + 1.f}, m_font, shadow, kPercentageScale);
        ren.drawText(buf, {tx, ty}, m_font, text, kPercentageScale);
        ren.drawText(buf, {tx + 0.45f, ty}, m_font, text.withAlpha(0.48f * op), kPercentageScale);
    }
}

nxui::Vec2 BatteryWidget::computeContentSize() const {
    nxui::Vec2 textBase =
        m_font ? m_font->measure("100%") : nxui::Vec2{42.f, 18.f};

    const float textW = textBase.x * kPercentageScale;
    const float textH = textBase.y * kPercentageScale;

    return {
        kBatteryWidth + 14.f + textW,
        std::max(kBatteryHeight, textH)
    };
}
