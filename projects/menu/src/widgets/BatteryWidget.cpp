#include "BatteryWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <switch.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace {

constexpr float kBatteryWidth = 58.f;
constexpr float kBatteryHeight = 28.f;
constexpr float kPercentageScale = 1.12f;

nxui::Vec2 boltPoint(const nxui::Rect& r, float x, float y) {
    return {
        r.x + (x / 16.f) * r.width,
        r.y + (y / 16.f) * r.height
    };
}

void drawLightningBolt(nxui::Renderer& ren,
                       const nxui::Rect& r,
                       const nxui::Color& fill,
                       const nxui::Color& edge,
                       float outlineThickness) {
    nxui::Vec2 p0 = boltPoint(r, 4.732f, 7.95335f);
    nxui::Vec2 p1 = boltPoint(r, 6.90908f, 2.f);
    nxui::Vec2 p2 = boltPoint(r, 10.54547f, 2.f);
    nxui::Vec2 p3 = boltPoint(r, 8.36364f, 7.01316f);
    nxui::Vec2 p4 = boltPoint(r, 11.27275f, 7.01316f);
    nxui::Vec2 p5 = boltPoint(r, 4.72725f, 14.f);
    nxui::Vec2 p6 = boltPoint(r, 6.93656f, 7.95135f);

    ren.drawTriangle(p0, p1, p2, fill);
    ren.drawTriangle(p0, p2, p3, fill);
    ren.drawTriangle(p0, p3, p6, fill);
    ren.drawTriangle(p6, p3, p4, fill);
    ren.drawTriangle(p6, p4, p5, fill);

    if (outlineThickness > 0.f) {
        ren.drawLine(p0, p1, edge, outlineThickness);
        ren.drawLine(p1, p2, edge, outlineThickness);
        ren.drawLine(p2, p3, edge, outlineThickness);
        ren.drawLine(p3, p4, edge, outlineThickness);
        ren.drawLine(p4, p5, edge, outlineThickness);
        ren.drawLine(p5, p6, edge, outlineThickness);
        ren.drawLine(p6, p0, edge, outlineThickness);
    }
}

} // namespace

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

    float level = std::clamp(m_level, 0.f, 1.f);

    char buf[16];
    std::snprintf(
        buf,
        sizeof(buf),
        "%d%%",
        static_cast<int>(level * 100)
    );

    nxui::Vec2 textBase =
        m_font
            ? m_font->measure(buf)
            : nxui::Vec2{42.f, 18.f};

    const float textW =
        textBase.x * kPercentageScale;

    const float textH =
        textBase.y * kPercentageScale;

    const float bw = kBatteryWidth;
    const float bh = kBatteryHeight;

    const float boltSlotW =
        m_charging ? 26.f : 0.f;

    const float chargeGap =
        m_charging ? 9.f : 0.f;

    constexpr float kTextGap = 14.f;

    const float batteryGroupW =
        bw + chargeGap + boltSlotW;

    const float groupW =
        batteryGroupW + kTextGap + textW;

    const float groupH =
        std::max(bh, textH);

    const float bx =
        cr.x + (cr.width - groupW) * 0.5f;

    const float by =
        cr.y + (cr.height - groupH) * 0.5f +
        (groupH - bh) * 0.5f;

    nxui::Rect body = {bx, by, bw, bh};
    const float radius = bh * 0.44f;

    nxui::Color shell =
        m_textColor.withAlpha(0.22f * op);

    nxui::Color shellTop =
        nxui::Color::white().withAlpha(0.18f * op);

    nxui::Color shellEdge =
        m_textColor.withAlpha(0.66f * op);

    nxui::Color terminal =
        m_textColor.withAlpha(0.48f * op);

    ren.drawRoundedRect(body, shell, radius);

    ren.drawRoundedRect(
        {
            body.x + 2.4f,
            body.y + 2.2f,
            body.width - 4.8f,
            body.height * 0.42f
        },
        shellTop,
        radius * 0.72f
    );

    ren.drawRoundedRectOutline(
        body,
        shellEdge,
        radius,
        1.65f
    );

    ren.drawRoundedRect(
        {
            body.right() + 2.f,
            body.y + bh * 0.32f,
            5.6f,
            bh * 0.36f
        },
        terminal,
        2.8f
    );

    const float chargePulse =
        m_charging
            ? 0.74f +
                0.26f *
                (0.5f +
                 0.5f *
                 std::sin(m_chargeAnim * 5.2f))
            : 1.f;

    nxui::Color fill =
        level > 0.20f
            ? nxui::Color(
                0.32f,
                0.93f,
                0.52f,
                op * chargePulse
              )
            : nxui::Color(
                0.95f,
                0.24f,
                0.20f,
                op
              );

    if (m_charging && level > 0.20f) {
        fill = nxui::Color(
            0.46f,
            0.96f,
            0.66f,
            op * chargePulse
        );
    }

    nxui::Rect inner = body.shrunk(4.f);
    const float innerW =
        std::max(0.f, inner.width * level);

    if (innerW > 0.5f) {
        nxui::Rect fillRect = {
            inner.x,
            inner.y,
            innerW,
            inner.height
        };

        ren.drawRoundedRect(
            fillRect,
            fill,
            std::min(
                radius * 0.68f,
                fillRect.width * 0.5f
            )
        );

        ren.drawRoundedRect(
            {
                fillRect.x + 1.5f,
                fillRect.y + 1.3f,
                std::max(
                    0.f,
                    fillRect.width - 3.f
                ),
                fillRect.height * 0.34f
            },
            nxui::Color::white().withAlpha(
                0.18f * op * chargePulse
            ),
            std::min(
                radius * 0.45f,
                fillRect.width * 0.45f
            )
        );
    }

    if (m_charging) {
        const float boltPulse =
            0.70f +
            0.30f *
            (0.5f +
             0.5f *
             std::sin(m_chargeAnim * 6.8f));

        const float boltH =
            32.f + 1.8f * boltPulse;

        const float boltW =
            boltH * 0.78f;

        const float boltX =
            body.right() +
            chargeGap +
            (boltSlotW - boltW) * 0.5f;

        const float boltY =
            by +
            (bh - boltH) * 0.5f -
            0.5f;

        nxui::Rect boltRect = {
            boltX,
            boltY,
            boltW,
            boltH
        };

        nxui::Color glow =
            nxui::Color(
                1.f,
                0.74f,
                0.12f,
                0.18f * op * boltPulse
            );

        drawLightningBolt(
            ren,
            boltRect.expanded(3.f),
            glow,
            glow.withAlpha(0.f),
            0.f
        );

        nxui::Color boltColor =
            nxui::Color(
                1.f,
                0.86f,
                0.18f,
                op *
                (0.88f + 0.12f * boltPulse)
            );

        nxui::Color boltEdge =
            nxui::Color(
                1.f,
                0.64f,
                0.08f,
                op * 0.72f
            );

        drawLightningBolt(
            ren,
            boltRect,
            boltColor,
            boltEdge,
            1.25f
        );
    }

    if (m_font) {
        const float tx =
            bx + batteryGroupW + kTextGap;

        const float ty =
            cr.y + (cr.height - textH) * 0.5f;

        const nxui::Color shadow =
            nxui::Color(
                0.f,
                0.f,
                0.f,
                0.36f * op
            );

        const nxui::Color text =
            m_textColor.withAlpha(op);

        ren.drawText(
            buf,
            {tx + 1.1f, ty + 1.1f},
            m_font,
            shadow,
            kPercentageScale
        );

        ren.drawText(
            buf,
            {tx, ty},
            m_font,
            text,
            kPercentageScale
        );

        ren.drawText(
            buf,
            {tx + 0.55f, ty},
            m_font,
            text.withAlpha(0.52f * op),
            kPercentageScale
        );
    }
}

nxui::Vec2 BatteryWidget::computeContentSize() const {
    nxui::Vec2 textBase =
        m_font
            ? m_font->measure("100%")
            : nxui::Vec2{42.f, 18.f};

    const float textW =
        textBase.x * kPercentageScale;

    const float textH =
        textBase.y * kPercentageScale;

    constexpr float kTextGap = 14.f;
    constexpr float kMaximumBoltSpace = 35.f;

    return {
        kBatteryWidth +
            kMaximumBoltSpace +
            kTextGap +
            textW,
        std::max(kBatteryHeight, textH)
    };
}
