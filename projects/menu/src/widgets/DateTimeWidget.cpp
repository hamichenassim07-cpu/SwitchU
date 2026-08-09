#include "DateTimeWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include "HomeLiquidGlassStyle.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cstdio>

namespace {
constexpr float kTimeScale = 1.28f;
constexpr float kDateScale = 1.12f;
constexpr float kLineGap = 5.f;
constexpr float kTabAnimSpeed = 9.0f;

constexpr float kNavX = 455.f;
constexpr float kNavY = 18.f;
constexpr float kNavW = 370.f;
constexpr float kNavH = 48.f;
constexpr float kNavInset = 6.f;
constexpr float kTabGap = 4.f;
constexpr float kTabW = (kNavW - kNavInset * 2.f - kTabGap) * 0.5f;
constexpr float kTabH = 36.f;

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float smooth01(float value) {
    value = clamp01(value);
    return value * value * (3.f - 2.f * value);
}
}

DateTimeWidget::DateTimeWidget() {
    // V10.2: use the real nxui LiquidGlass shader instead of stacking flat
    // translucent rectangles. Wide category glass is rendered explicitly in
    // onContentRender with the same captured backdrop.
    setCornerRadius(22.f);
    setBaseColor(nxui::Color(0.48f, 0.72f, 0.62f, 0.30f));
    setBorderColor(nxui::Color(0.88f, 1.00f, 0.94f, 0.28f));
    setHighlightColor(nxui::Color(1.f, 1.f, 1.f, 0.14f));
    setPanelOpacity(0.90f);
    setLiquidGlassEnabled(true);
    setLiquidGlassShaderEnabled(true);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
}

void DateTimeWidget::onRender(nxui::Renderer& ren) {
    const nxui::LiquidGlassSettings saved = ren.liquidGlassSettings();
    switchu::homeui::applyLiquidGlassV102(ren);
    nxui::GlassWidget::onRender(ren);
    ren.liquidGlassSettings() = saved;
}

nxui::Rect DateTimeWidget::homeTabsRect() const {
    return {kNavX, kNavY, kNavW, kNavH};
}

nxui::Rect DateTimeWidget::activeHomeTabRect() const {
    const float gamesX = kNavX + kNavInset;
    const float appsX = gamesX + kTabW + kTabGap;
    return {
        m_homeApplicationsActive ? appsX : gamesX,
        kNavY + kNavInset,
        kTabW,
        kTabH
    };
}

void DateTimeWidget::setUse12HourClock(bool enabled) {
    if (m_use12HourClock == enabled)
        return;

    m_use12HourClock = enabled;
    m_timeStr.clear();
    m_timer = 1.f;
}

void DateTimeWidget::onContentUpdate(float dt) {
    // V10: animate the category capsule every frame, independently from the
    // once-per-second clock refresh.
    const float target = m_homeApplicationsActive ? 1.f : 0.f;
    const float amount = std::min(1.f, std::max(0.f, dt) * kTabAnimSpeed);
    m_homeTabSlide += (target - m_homeTabSlide) * amount;
    if (std::abs(target - m_homeTabSlide) < 0.001f)
        m_homeTabSlide = target;

    m_timer += dt;

    if (m_timer < 1.f && !m_timeStr.empty())
        return;

    m_timer = 0.f;

    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);

    if (!tm)
        return;

    char buf[64];

    if (m_use12HourClock) {
        int hour = tm->tm_hour % 12;

        if (hour == 0)
            hour = 12;

        std::snprintf(
            buf,
            sizeof(buf),
            "%d:%02d %s",
            hour,
            tm->tm_min,
            tm->tm_hour >= 12 ? "PM" : "AM"
        );
    } else {
        std::snprintf(
            buf,
            sizeof(buf),
            "%02d:%02d",
            tm->tm_hour,
            tm->tm_min
        );
    }

    m_timeStr = buf;

    std::snprintf(
        buf,
        sizeof(buf),
        "%02d/%02d/%04d",
        tm->tm_mday,
        tm->tm_mon + 1,
        tm->tm_year + 1900
    );

    m_dateStr = buf;
}

void DateTimeWidget::onContentRender(nxui::Renderer& ren) {
    if (!m_font)
        return;

    nxui::Rect cr = contentRect();

    nxui::Font* dateFont =
        m_smallFont ? m_smallFont : m_font;

    nxui::Vec2 timeBase = m_font->measure(m_timeStr);
    nxui::Vec2 dateBase = dateFont->measure(m_dateStr);

    nxui::Vec2 timeSize = {
        timeBase.x * kTimeScale,
        timeBase.y * kTimeScale
    };

    nxui::Vec2 dateSize = {
        dateBase.x * kDateScale,
        dateBase.y * kDateScale
    };

    const float contentH =
        timeSize.y + kLineGap + dateSize.y;

    const float timeX =
        cr.x + (cr.width - timeSize.x) * 0.5f;

    const float timeY =
        cr.y + (cr.height - contentH) * 0.5f;

    const float dateX =
        cr.x + (cr.width - dateSize.x) * 0.5f;

    const float dateY =
        timeY + timeSize.y + kLineGap;

    const nxui::Color shadow =
        nxui::Color(0.f, 0.f, 0.f, 0.34f * m_opacity);

    const nxui::Color primary =
        m_textColor.withAlpha(m_opacity);

    const nxui::Color secondary =
        m_secondaryColor.withAlpha(0.98f * m_opacity);

    ren.drawText(
        m_timeStr,
        {timeX + 1.1f, timeY + 1.2f},
        m_font,
        shadow,
        kTimeScale
    );

    ren.drawText(
        m_timeStr,
        {timeX, timeY},
        m_font,
        primary,
        kTimeScale
    );

    ren.drawText(
        m_timeStr,
        {timeX + 0.55f, timeY},
        m_font,
        primary.withAlpha(0.52f * m_opacity),
        kTimeScale
    );

    ren.drawText(
        m_dateStr,
        {dateX + 1.f, dateY + 1.f},
        dateFont,
        shadow,
        kDateScale
    );

    ren.drawText(
        m_dateStr,
        {dateX, dateY},
        dateFont,
        secondary,
        kDateScale
    );

    ren.drawText(
        m_dateStr,
        {dateX + 0.45f, dateY},
        dateFont,
        secondary.withAlpha(0.48f * m_opacity),
        kDateScale
    );

    // V10 centered HOME categories. There is intentionally no Nintendo eShop
    // entry. The indicator is a real animated state controlled by the menu.
    const nxui::Rect navRect = homeTabsRect();
    const float gamesX = kNavX + kNavInset;
    const float appsX = gamesX + kTabW + kTabGap;
    const float slide = smooth01(m_homeTabSlide);
    const nxui::Rect activeRect {
        gamesX + (appsX - gamesX) * slide,
        kNavY + kNavInset,
        kTabW,
        kTabH
    };

    // V10.2: real refractive/frosted glass. Reuse the capture already made
    // by the clock GlassWidget when possible, avoiding a second full-screen
    // backdrop copy in the same frame.
    ren.captureToOffscreen(true);
    ren.drawLiquidGlass(
        0, navRect, 24.f,
        nxui::Color(0.50f, 0.80f, 0.66f, 0.48f),
        0.93f * m_opacity, 0.f
    );
    ren.drawRoundedRectOutline(
        navRect,
        nxui::Color(0.91f, 1.00f, 0.96f, 0.24f * m_opacity),
        24.f, 1.f
    );

    // The active category is a brighter internal lens, not a focus cursor.
    ren.drawLiquidGlass(
        0, activeRect, 18.f,
        nxui::Color(0.86f, 1.00f, 0.92f, 0.82f),
        0.82f * m_opacity, 0.f
    );
    ren.drawRoundedRectOutline(
        activeRect,
        nxui::Color(1.f, 1.f, 1.f, 0.22f * m_opacity),
        18.f, 1.f
    );

    auto& i18n = nxui::I18n::instance();
    const std::string games =
        i18n.tr("home.tabs.games", "Jeux");
    const std::string apps =
        i18n.tr("home.tabs.apps", "Applications");

    const nxui::Vec2 gamesSz = dateFont->measure(games);
    const nxui::Vec2 appsSz = dateFont->measure(apps);
    const float gamesCenterX = gamesX + kTabW * 0.5f;
    const float appsCenterX = appsX + kTabW * 0.5f;
    const float textY = kNavY + (kNavH - gamesSz.y) * 0.5f;

    const float gamesActive = 1.f - slide;
    const float appsActive = slide;

    const nxui::Color inactive(0.96f, 0.99f, 0.97f, 0.84f * m_opacity);
    const nxui::Color active(0.075f, 0.105f, 0.090f, m_opacity);

    auto mixColor = [](const nxui::Color& a,
                       const nxui::Color& b,
                       float t) {
        t = clamp01(t);
        return nxui::Color(
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        );
    };

    ren.drawText(
        games,
        {gamesCenterX - gamesSz.x * 0.5f, textY},
        dateFont,
        mixColor(inactive, active, gamesActive),
        1.f
    );

    const float appsTextY =
        kNavY + (kNavH - appsSz.y) * 0.5f;
    ren.drawText(
        apps,
        {appsCenterX - appsSz.x * 0.5f, appsTextY},
        dateFont,
        mixColor(inactive, active, appsActive),
        1.f
    );
}

nxui::Vec2 DateTimeWidget::computeContentSize() const {
    if (!m_font)
        return {190.f, 70.f};

    nxui::Font* dateFont =
        m_smallFont ? m_smallFont : m_font;

    nxui::Vec2 timeBase =
        m_font->measure(
            m_use12HourClock ? "12:00 PM" : "00:00"
        );

    nxui::Vec2 dateBase =
        dateFont->measure("00/00/0000");

    const float width = std::max(
        timeBase.x * kTimeScale,
        dateBase.x * kDateScale
    );

    const float height =
        timeBase.y * kTimeScale +
        kLineGap +
        dateBase.y * kDateScale;

    return {width, height};
}
