#include "DateTimeWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include "HomeLiquidGlassStyle.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <chrono>
#include <cstdio>

namespace {
constexpr float kTimeScale = 1.28f;
constexpr float kTabAnimSpeed = 9.0f;

constexpr float kNavX = 440.f;
constexpr float kNavY = 18.f;
constexpr float kNavW = 400.f;
constexpr float kNavH = 54.f;
constexpr float kNavInset = 7.f;
constexpr float kTabGap = 6.f;
constexpr float kTabW = (kNavW - kNavInset * 2.f - kTabGap) * 0.5f;
constexpr float kTabH = 40.f;

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
    setBaseColor(nxui::Color(0.56f, 0.66f, 0.82f, 0.22f));
    setBorderColor(nxui::Color(0.92f, 0.97f, 1.00f, 0.40f));
    setHighlightColor(nxui::Color(1.f, 1.f, 1.f, 0.22f));
    // V10.6: clock/date are text-only. Liquid Glass remains reserved for
    // the Jeux / Applications selector rendered explicitly below.
    setPanelOpacity(0.f);
    setLiquidGlassEnabled(false);
    setLiquidGlassShaderEnabled(false);
    setForceLiquidGlass(false);
    setBorderWidth(0.f);
    setBlurEnabled(false);
}

void DateTimeWidget::onRender(nxui::Renderer& ren) {
    const nxui::LiquidGlassSettings saved = ren.liquidGlassSettings();
    switchu::homeui::applyLiquidGlassV105(ren);
    nxui::GlassWidget::onRender(ren);
    ren.liquidGlassSettings() = saved;
}

nxui::Rect DateTimeWidget::homeTabsRect() const {
    return {kNavX, kNavY, kNavW, kNavH};
}

void DateTimeWidget::setHomeApplicationsActive(bool active) {
    if (m_homeApplicationsActive == active)
        return;

    m_homeApplicationsActive = active;
    m_homeTabPop = 1.f;
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

    m_homeTabPop += (0.f - m_homeTabPop) * std::min(1.f, std::max(0.f, dt) * 9.5f);
    if (std::abs(m_homeTabPop) < 0.001f)
        m_homeTabPop = 0.f;

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
    nxui::Vec2 timeSize = {
        timeBase.x * kTimeScale,
        timeBase.y * kTimeScale
    };

    const float timeX =
        cr.x + (cr.width - timeSize.x) * 0.5f;

    const float timeY =
        cr.y + (cr.height - timeSize.y) * 0.5f;

    const nxui::Color shadow =
        nxui::Color(0.f, 0.f, 0.f, 0.34f * m_opacity);

    const nxui::Color primary =
        m_textColor.withAlpha(m_opacity);


    // V10.6: use the exact V7.4.4 lockscreen rhythm: a hard 800 ms
    // alarm-clock blink with fixed geometry so the minutes never shift.
    const auto blinkMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    const bool separatorVisible = ((blinkMs / 800) % 2) == 0;

    const std::size_t colonPos = m_timeStr.find(':');
    if (colonPos != std::string::npos) {
        const std::string hourPart = m_timeStr.substr(0, colonPos);
        const std::string minutePart = m_timeStr.substr(colonPos + 1);
        const float hourW = m_font->measure(hourPart).x * kTimeScale;
        const float colonW = m_font->measure(":").x * kTimeScale;
        const float minuteX = timeX + hourW + colonW;

        auto drawTimePiece = [&](const std::string& text, float x) {
            ren.drawText(text, {x + 1.1f, timeY + 1.2f}, m_font,
                         shadow, kTimeScale);
            ren.drawText(text, {x, timeY}, m_font,
                         primary, kTimeScale);
            ren.drawText(text, {x + 0.55f, timeY}, m_font,
                         primary.withAlpha(0.52f * m_opacity), kTimeScale);
        };

        drawTimePiece(hourPart, timeX);
        if (separatorVisible)
            drawTimePiece(":", timeX + hourW);
        drawTimePiece(minutePart, minuteX);
    } else {
        ren.drawText(m_timeStr, {timeX + 1.1f, timeY + 1.2f}, m_font,
                     shadow, kTimeScale);
        ren.drawText(m_timeStr, {timeX, timeY}, m_font,
                     primary, kTimeScale);
    }

    // V10.7: date intentionally removed from the HOME HUD.

    // V10 centered HOME categories. There is intentionally no Nintendo eShop
    // entry. The indicator is a real animated state controlled by the menu.
    const nxui::Rect navRect = homeTabsRect();
    const float gamesX = kNavX + kNavInset;
    const float appsX = gamesX + kTabW + kTabGap;
    const float slide = smooth01(m_homeTabSlide);
    nxui::Rect activeRect {
        gamesX + (appsX - gamesX) * slide,
        kNavY + kNavInset,
        kTabW,
        kTabH
    };

    // V10.8: bubble-like tab response. The active lens swells softly and
    // relaxes back after each L/R category switch, instead of only sliding.
    const float pop = clamp01(m_homeTabPop);
    const float expandX = 8.f * pop;
    const float expandY = 3.5f * pop;
    activeRect.x -= expandX * 0.5f;
    activeRect.y -= expandY * 0.5f;
    activeRect.width += expandX;
    activeRect.height += expandY;

    // V10.4 C2: the whole category switch remains real refractive glass.
    // The active side adds a milky-white lens on top, so the current category
    // stays unmistakable even when the backdrop is bright or very colourful.
    ren.captureToOffscreen(true);
    ren.drawLiquidGlass(
        0, navRect, 24.f,
        nxui::Color(0.70f, 0.82f, 0.98f, 0.46f),
        0.98f * m_opacity, 0.f
    );
    ren.drawRoundedRectOutline(
        navRect,
        nxui::Color(0.98f, 1.00f, 1.00f, 0.38f * m_opacity),
        24.f, 1.f
    );

    ren.drawLiquidGlass(
        0, activeRect, 18.f,
        nxui::Color(1.00f, 1.00f, 1.00f, 0.96f),
        0.98f * m_opacity, 0.f
    );
    ren.drawRoundedRect(
        activeRect.shrunk(1.4f),
        nxui::Color(1.00f, 1.00f, 1.00f, 0.62f * m_opacity),
        16.8f
    );
    ren.drawRoundedRectOutline(
        activeRect,
        nxui::Color(1.f, 1.f, 1.f, 0.92f * m_opacity),
        18.f, 1.2f
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

    const nxui::Color inactive(0.985f, 0.99f, 1.00f, 0.98f * m_opacity);
    const nxui::Color active(0.045f, 0.052f, 0.065f, 0.98f * m_opacity);

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

    const nxui::Color darkShadow(0.0f, 0.0f, 0.0f, 0.38f * m_opacity);
    const nxui::Color lightShadow(1.0f, 1.0f, 1.0f, 0.18f * m_opacity);
    const nxui::Color gamesColor = mixColor(inactive, active, gamesActive);
    const nxui::Color gamesShadow = mixColor(darkShadow, lightShadow, gamesActive);
    ren.drawText(
        games,
        {gamesCenterX - gamesSz.x * 0.5f + 1.f, textY + 1.3f},
        dateFont,
        gamesShadow,
        1.f
    );
    ren.drawText(
        games,
        {gamesCenterX - gamesSz.x * 0.5f, textY},
        dateFont,
        gamesColor,
        1.f
    );

    const float appsTextY =
        kNavY + (kNavH - appsSz.y) * 0.5f;
    const nxui::Color appsColor = mixColor(inactive, active, appsActive);
    const nxui::Color appsShadow = mixColor(darkShadow, lightShadow, appsActive);
    ren.drawText(
        apps,
        {appsCenterX - appsSz.x * 0.5f + 1.f, appsTextY + 1.3f},
        dateFont,
        appsShadow,
        1.f
    );
    ren.drawText(
        apps,
        {appsCenterX - appsSz.x * 0.5f, appsTextY},
        dateFont,
        appsColor,
        1.f
    );
}

nxui::Vec2 DateTimeWidget::computeContentSize() const {
    if (!m_font)
        return {190.f, 70.f};

    nxui::Vec2 timeBase =
        m_font->measure(
            m_use12HourClock ? "12:00 PM" : "00:00"
        );

    return {
        timeBase.x * kTimeScale,
        timeBase.y * kTimeScale
    };
}
