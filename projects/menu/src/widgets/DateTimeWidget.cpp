#include "DateTimeWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <ctime>
#include <cstdio>

namespace {
constexpr float kTimeScale = 1.28f;
constexpr float kDateScale = 1.12f;
constexpr float kLineGap = 5.f;
}

void DateTimeWidget::setUse12HourClock(bool enabled) {
    if (m_use12HourClock == enabled)
        return;

    m_use12HourClock = enabled;
    m_timeStr.clear();
    m_timer = 1.f;
}

void DateTimeWidget::onContentUpdate(float dt) {
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

    // Shadow + a tiny second pass make the existing font easier to read.
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

    // V9 top navigation scaffold, visually matching the reference. "Jeux" is
    // the active HOME section; Applications is kept subtle until its filtering
    // logic is wired in a later pass.
    const nxui::Rect navRect {455.f, 18.f, 370.f, 48.f};
    const nxui::Rect activeRect {461.f, 24.f, 146.f, 36.f};

    ren.drawRoundedRect(
        navRect,
        nxui::Color(0.035f, 0.032f, 0.045f, 0.52f * m_opacity),
        24.f
    );
    ren.drawRoundedRectOutline(
        navRect,
        nxui::Color(1.f, 1.f, 1.f, 0.12f * m_opacity),
        24.f,
        1.f
    );
    ren.drawRoundedRect(
        activeRect,
        nxui::Color(0.94f, 0.94f, 0.96f, 0.96f * m_opacity),
        18.f
    );

    auto& i18n = nxui::I18n::instance();
    const std::string games =
        i18n.tr("home.tabs.games", "Jeux");
    const std::string apps =
        i18n.tr("home.tabs.apps", "Applications");

    const nxui::Vec2 gamesSz = dateFont->measure(games);
    const nxui::Vec2 appsSz = dateFont->measure(apps);

    ren.drawText(
        games,
        {activeRect.x + (activeRect.width - gamesSz.x) * 0.5f,
         activeRect.y + (activeRect.height - gamesSz.y) * 0.5f},
        dateFont,
        nxui::Color(0.08f, 0.075f, 0.10f, m_opacity),
        1.f
    );

    const float appsCenterX =
        activeRect.x + activeRect.width +
        (navRect.x + navRect.width - activeRect.x - activeRect.width) * 0.5f;

    ren.drawText(
        apps,
        {appsCenterX - appsSz.x * 0.5f,
         navRect.y + (navRect.height - appsSz.y) * 0.5f},
        dateFont,
        nxui::Color(0.95f, 0.94f, 0.98f, 0.72f * m_opacity),
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
