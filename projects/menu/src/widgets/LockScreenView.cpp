#include "LockScreenView.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float easeOutCubic(float value) {
    value = clamp01(value);
    const float inv = 1.f - value;
    return 1.f - inv * inv * inv;
}

nxui::Color mixColor(const nxui::Color& a,
                     const nxui::Color& b,
                     float t) {
    t = clamp01(t);
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

nxui::Color lockGradient(float t) {
    const nxui::Color electricBlue(0.08f, 0.58f, 1.00f, 1.f);
    const nxui::Color purple(0.38f, 0.08f, 0.82f, 1.f);
    const nxui::Color darkFuchsia(0.74f, 0.08f, 0.54f, 1.f);
    t = clamp01(t);
    if (t < 0.52f)
        return mixColor(electricBlue, purple, t / 0.52f);
    return mixColor(purple, darkFuchsia, (t - 0.52f) / 0.48f);
}

void drawArc(nxui::Renderer& ren,
             const nxui::Vec2& center,
             float radius,
             float startAngle,
             float endAngle,
             const nxui::Color& color,
             float thickness,
             int segments) {
    segments = std::max(4, segments);
    nxui::Vec2 previous = {
        center.x + std::cos(startAngle) * radius,
        center.y + std::sin(startAngle) * radius
    };
    for (int i = 1; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float angle = startAngle + (endAngle - startAngle) * t;
        const nxui::Vec2 current = {
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        };
        ren.drawLine(previous, current, color, thickness);
        previous = current;
    }
}

nxui::Vec2 roundedPerimeterPoint(const nxui::Rect& rect,
                                 float radius,
                                 float u) {
    const float width = std::max(1.f, rect.width);
    const float height = std::max(1.f, rect.height);
    const float r = std::clamp(radius, 1.f, std::min(width, height) * 0.5f);
    const float horizontal = std::max(0.f, width - 2.f * r);
    const float vertical = std::max(0.f, height - 2.f * r);
    const float quarterArc = 0.5f * kPi * r;
    const float perimeter = 2.f * horizontal + 2.f * vertical + 4.f * quarterArc;
    float distance = std::fmod(std::max(0.f, u), 1.f) * perimeter;

    if (distance <= horizontal)
        return {rect.x + r + distance, rect.y};
    distance -= horizontal;
    if (distance <= quarterArc) {
        const float angle = -0.5f * kPi + distance / r;
        return {rect.x + width - r + std::cos(angle) * r,
                rect.y + r + std::sin(angle) * r};
    }
    distance -= quarterArc;
    if (distance <= vertical)
        return {rect.x + width, rect.y + r + distance};
    distance -= vertical;
    if (distance <= quarterArc) {
        const float angle = distance / r;
        return {rect.x + width - r + std::cos(angle) * r,
                rect.y + height - r + std::sin(angle) * r};
    }
    distance -= quarterArc;
    if (distance <= horizontal)
        return {rect.x + width - r - distance, rect.y + height};
    distance -= horizontal;
    if (distance <= quarterArc) {
        const float angle = 0.5f * kPi + distance / r;
        return {rect.x + r + std::cos(angle) * r,
                rect.y + height - r + std::sin(angle) * r};
    }
    distance -= quarterArc;
    if (distance <= vertical)
        return {rect.x, rect.y + height - r - distance};
    distance -= vertical;
    const float angle = kPi + distance / r;
    return {rect.x + r + std::cos(angle) * r,
            rect.y + r + std::sin(angle) * r};
}

void drawGradientRoundedOutline(nxui::Renderer& ren,
                                const nxui::Rect& rect,
                                float radius,
                                float thickness,
                                float alpha,
                                float phase) {
    constexpr int segments = 112;
    nxui::Vec2 previous = roundedPerimeterPoint(rect, radius, 0.f);
    for (int i = 1; i <= segments; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(segments);
        const nxui::Vec2 current = roundedPerimeterPoint(rect, radius, u);
        const nxui::Color color = lockGradient(std::fmod(u + phase, 1.f))
            .withAlpha(alpha);
        ren.drawLine(previous, current, color, thickness);
        previous = current;
    }
}

std::string utf8Codepoint(std::uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::vector<std::string> splitUtf8(const std::string& text) {
    std::vector<std::string> glyphs;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((lead & 0xE0u) == 0xC0u) length = 2;
        else if ((lead & 0xF0u) == 0xE0u) length = 3;
        else if ((lead & 0xF8u) == 0xF0u) length = 4;
        if (i + length > text.size()) length = 1;
        glyphs.push_back(text.substr(i, length));
        i += length;
    }
    return glyphs;
}

void buildClockStrings(bool use12Hour,
                       std::string& timeText,
                       std::string& dateText) {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) {
        timeText = "--:--";
        dateText = "Date indisponible";
        return;
    }

    char buffer[96] = {};
    if (use12Hour) {
        int hour = local->tm_hour % 12;
        if (hour == 0) hour = 12;
        std::snprintf(buffer, sizeof(buffer), "%d:%02d %s",
                      hour, local->tm_min,
                      local->tm_hour >= 12 ? "ap. m." : "mat.");
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d",
                      local->tm_hour, local->tm_min);
    }
    timeText = buffer;

    static constexpr const char* days[] = {
        "dimanche", "lundi", "mardi", "mercredi",
        "jeudi", "vendredi", "samedi"
    };
    static constexpr const char* months[] = {
        "janvier", "février", "mars", "avril", "mai", "juin",
        "juillet", "août", "septembre", "octobre", "novembre", "décembre"
    };
    const int weekday = std::clamp(local->tm_wday, 0, 6);
    const int month = std::clamp(local->tm_mon, 0, 11);
    std::snprintf(buffer, sizeof(buffer), "%s %d %s %04d",
                  days[weekday], local->tm_mday,
                  months[month], local->tm_year + 1900);
    dateText = buffer;
}

} // namespace

LockScreenView::LockScreenView() {
    setRect({0.f, 0.f, 1280.f, 720.f});

    m_infoPanel.setForceLiquidGlass(true);
    m_infoPanel.setBlurEnabled(false);
    m_infoPanel.setBackingEnabled(true);
    m_infoPanel.setBackingColor({0.010f, 0.012f, 0.035f, 1.f});
    m_infoPanel.setMaterialTextureEnabled(true);
    m_infoPanel.setMaterialTextureIntensity(0.45f);

    m_actionPanel.setForceLiquidGlass(true);
    m_actionPanel.setBlurEnabled(false);
    m_actionPanel.setBackingEnabled(true);
    m_actionPanel.setBackingColor({0.010f, 0.012f, 0.035f, 1.f});
    m_actionPanel.setMaterialTextureEnabled(true);
    m_actionPanel.setMaterialTextureIntensity(0.40f);

    m_gameIcon.setFocusable(false);
    m_gameIcon.setSuspended(true);
    m_gameIcon.setForceLiquidGlass(true);
    m_gameIcon.setBlurEnabled(false);
    m_gameIcon.forceVisible();

    m_battery.setForceLiquidGlass(true);
    m_battery.setBlurEnabled(false);
    m_battery.setSize(238.f, 92.f);

    m_titlePill.setPadding(9.f, 22.f, 9.f, 22.f);
    m_titlePill.setForceLiquidGlass(true);
    m_titlePill.setBlurEnabled(false);
}

void LockScreenView::setFonts(nxui::Font* normal,
                              nxui::Font* small,
                              nxui::Font* large,
                              nxui::Font* medium,
                              nxui::Font* icons) {
    m_fontNormal = normal;
    m_fontSmall = small;
    m_fontLarge = large;
    m_fontMedium = medium;
    m_fontIcons = icons;
    m_battery.setFont(normal);
    m_titlePill.setFont(normal);
}

void LockScreenView::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    m_progress.setTheme(theme);
    applyThemeToWidgets();
}

void LockScreenView::setGameCardTexture(nxui::Texture* texture) {
    m_gameCardTexture = texture;
    m_gameIcon.setGameCardTexture(texture);
}

void LockScreenView::setSuspendedGame(nxui::Texture* texture,
                                      const std::string& title,
                                      std::uint64_t titleId,
                                      bool gameCard) {
    m_hasGame = texture && texture->valid();
    m_gameTitle = title;
    const auto titleGlyphs = splitUtf8(title);
    std::string displayedTitle;
    const std::size_t titleLimit = 26;
    for (std::size_t i = 0; i < std::min(titleLimit, titleGlyphs.size()); ++i)
        displayedTitle += titleGlyphs[i];
    if (titleGlyphs.size() > titleLimit)
        displayedTitle += "…";

    m_gameIcon.setTexture(texture);
    m_gameIcon.setTitle(displayedTitle);
    m_gameIcon.setTitleId(titleId);
    m_gameIcon.setIsGameCard(gameCard);
    m_gameIcon.setGameCardTexture(m_gameCardTexture);
    m_gameIcon.setSuspended(true);
    m_gameIcon.forceVisible();

    if (m_hasGame && !title.empty()) {
        m_titlePill.setVisible(true);
        m_titlePill.setAnchor(310.f, 466.f, 1280.f);
        m_titlePill.setText(displayedTitle, 1280.f);
    } else {
        m_titlePill.setVisible(false);
    }
}

void LockScreenView::clearSuspendedGame() {
    m_hasGame = false;
    m_gameTitle.clear();
    m_gameIcon.setTexture(nullptr);
    m_gameIcon.setTitle({});
    m_gameIcon.setTitleId(0);
    m_gameIcon.setIsGameCard(false);
    m_titlePill.setVisible(false);
}

void LockScreenView::setBatteryStatus(std::uint32_t percentage, bool charging) {
    m_batteryPercent = std::min<std::uint32_t>(percentage, 100u);
    m_batteryCharging = charging;
    m_battery.setBatteryStatus(m_batteryPercent, m_batteryCharging);
}

void LockScreenView::setProgress(int progress, float flash) {
    m_pressCount = std::clamp(progress, 0, 3);
    m_pressFlash = clamp01(flash);
    m_progress.setProgress(m_pressCount);
    m_progress.setFlash(m_pressFlash);
}

void LockScreenView::setTransition(float opacity,
                                   float reveal,
                                   float unlockProgress,
                                   float pulse,
                                   bool unlocking) {
    m_viewOpacity = clamp01(opacity);
    m_reveal = clamp01(reveal);
    m_unlockProgress = clamp01(unlockProgress);
    m_pulse = pulse;
    m_unlocking = unlocking;
    m_progress.setPulse(pulse);
}

void LockScreenView::applyThemeToWidgets() {
    if (!m_theme)
        return;

    m_gameIcon.setBaseColor(m_theme->iconDefault);
    m_gameIcon.setBorderColor(m_theme->panelBorder);
    m_gameIcon.setHighlightColor(m_theme->panelHighlight);
    m_gameIcon.setCornerRadius(m_theme->iconCornerRadius);

    m_battery.setBaseColor(m_theme->panelBase);
    m_battery.setBorderColor(m_theme->panelBorder);
    m_battery.setHighlightColor(m_theme->panelHighlight);
    m_battery.setTextColor(m_theme->textPrimary);
    m_battery.setCornerRadius(m_theme->cellCornerRadius);

    m_titlePill.setBaseColor(m_theme->panelBase);
    m_titlePill.setBorderColor(m_theme->panelBorder);
    m_titlePill.setHighlightColor(m_theme->panelHighlight);
    m_titlePill.setTextColor(m_theme->textPrimary);

    m_infoPanel.setBaseColor({0.05f, 0.06f, 0.16f, 0.68f});
    m_infoPanel.setBorderColor(lockGradient(0.28f).withAlpha(0.34f));
    m_infoPanel.setHighlightColor({0.92f, 0.96f, 1.f, 0.15f});
    m_infoPanel.setBorderWidth(1.5f);
    m_infoPanel.setCornerRadius(42.f);

    m_actionPanel.setBaseColor({0.05f, 0.06f, 0.16f, 0.72f});
    m_actionPanel.setBorderColor(lockGradient(0.62f).withAlpha(0.34f));
    m_actionPanel.setHighlightColor({0.92f, 0.96f, 1.f, 0.14f});
    m_actionPanel.setBorderWidth(1.4f);
    m_actionPanel.setCornerRadius(36.f);
}

void LockScreenView::onUpdate(float dt) {
    if (m_hasGame) {
        m_gameIcon.update(dt);
        m_titlePill.update(dt);
    }
    m_battery.update(dt);
    m_progress.update(dt);
}

void LockScreenView::onRender(nxui::Renderer& ren) {
    const float contentAlpha = m_viewOpacity * easeOutCubic(m_reveal);
    if (contentAlpha <= 0.001f)
        return;

    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.24f);
    const float slowPulse = 0.5f + 0.5f * std::sin(m_pulse * 0.40f);
    const float lift = -22.f * m_unlockProgress;

    ren.drawGradientRect(
        {0.f, 0.f, 1280.f, 720.f},
        nxui::Color(0.006f, 0.008f, 0.024f, 0.99f * m_viewOpacity),
        nxui::Color(0.015f, 0.010f, 0.040f, 0.99f * m_viewOpacity)
    );
    ren.drawRect(
        {0.f, 0.f, 1280.f, 720.f},
        nxui::Color(0.005f, 0.006f, 0.018f, 0.62f * m_viewOpacity)
    );
    ren.drawGradientRect(
        {0.f, 0.f, 1280.f, 720.f},
        nxui::Color(0.12f, 0.045f, 0.22f, 0.10f * m_viewOpacity),
        nxui::Color(0.02f, 0.10f, 0.18f, 0.04f * m_viewOpacity)
    );

    ren.drawCircle(
        {170.f, 626.f + lift * 0.12f},
        294.f,
        nxui::Color(0.05f, 0.22f, 0.62f,
                    (0.075f + 0.020f * slowPulse) * contentAlpha),
        92
    );
    ren.drawCircle(
        {1120.f, 88.f + lift * 0.08f},
        286.f,
        nxui::Color(0.32f, 0.05f, 0.52f,
                    (0.070f + 0.025f * breathe) * contentAlpha),
        92
    );

    const nxui::Vec2 artCenter = {310.f, 300.f + lift * 0.10f};
    drawArc(ren, artCenter, 258.f, -2.95f, 1.92f,
            nxui::Color(0.24f, 0.56f, 1.f,
                        (0.19f + 0.05f * breathe) * contentAlpha),
            2.2f, 112);
    drawArc(ren, artCenter, 294.f, -0.22f, 3.00f,
            nxui::Color(0.70f, 0.30f, 1.f,
                        (0.14f + 0.03f * slowPulse) * contentAlpha),
            1.7f, 108);
    drawArc(ren, artCenter, 342.f, -2.56f, 0.48f,
            nxui::Color(1.f, 0.72f, 0.26f, 0.085f * contentAlpha),
            1.2f, 96);

    if (m_hasGame) {
        m_gameIcon.setRect({176.f, 160.f + lift * 0.10f, 268.f, 268.f});
        m_gameIcon.setOpacity(contentAlpha);
        m_gameIcon.render(ren);

        m_titlePill.setOpacity(contentAlpha);
        m_titlePill.render(ren);
    }

    const nxui::Rect infoRect = m_hasGame
        ? nxui::Rect{610.f, 118.f + lift * 0.05f, 540.f, 390.f}
        : nxui::Rect{370.f, 118.f + lift * 0.05f, 540.f, 390.f};
    m_infoPanel.setRect(infoRect);
    m_infoPanel.setOpacity(contentAlpha);
    m_infoPanel.render(ren);
    drawGradientRoundedOutline(
        ren,
        infoRect.shrunk(1.2f),
        40.8f,
        1.35f,
        0.24f * contentAlpha,
        std::fmod(m_pulse * 0.035f, 1.f)
    );

    std::string timeText;
    std::string dateText;
    buildClockStrings(m_use12Hour, timeText, dateText);

    const float right = infoRect.right() - 50.f;
    if (m_fontLarge) {
        const float scale = 1.15f;
        const nxui::Vec2 size = m_fontLarge->measure(timeText);
        const float x = right - size.x * scale;
        const float y = infoRect.y + 36.f;
        ren.drawText(timeText, {x + 3.f, y + 4.f}, m_fontLarge,
                     nxui::Color(0.f, 0.f, 0.f, 0.30f * contentAlpha), scale);
        ren.drawText(timeText, {x, y}, m_fontLarge,
                     nxui::Color(0.97f, 0.98f, 1.f, 1.f * contentAlpha), scale);
    }

    if (m_fontMedium) {
        const float dateScale = 0.98f;
        const nxui::Vec2 dateSize = m_fontMedium->measure(dateText);
        ren.drawText(dateText,
                     {right - dateSize.x * dateScale, infoRect.y + 128.f},
                     m_fontMedium,
                     nxui::Color(0.84f, 0.89f, 0.98f, 0.90f * contentAlpha),
                     dateScale);

        const auto glyphs = splitUtf8(m_greeting);
        float greetingBaseWidth = 0.f;
        for (const auto& glyph : glyphs)
            greetingBaseWidth += m_fontMedium->measure(glyph).x;
        const float greetingMaxWidth = infoRect.width - 92.f;
        const float greetingScale = std::clamp(
            greetingBaseWidth > 0.f ? greetingMaxWidth / greetingBaseWidth : 0.98f,
            0.70f,
            0.98f
        );
        const float totalWidth = greetingBaseWidth * greetingScale;
        float cursorX = right - totalWidth;
        const float y = infoRect.y + 226.f;
        const float denom = std::max(1.f, static_cast<float>(glyphs.size() - 1));
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            const float t = static_cast<float>(i) / denom;
            const nxui::Color color = lockGradient(t).withAlpha(0.98f * contentAlpha);
            ren.drawText(glyphs[i], {cursorX, y}, m_fontMedium, color, greetingScale);
            cursorX += m_fontMedium->measure(glyphs[i]).x * greetingScale;
        }
    }

    m_battery.setPosition(infoRect.right() - 50.f - 238.f,
                          infoRect.y + 278.f);
    m_battery.setSize(238.f, 92.f);
    m_battery.setOpacity(contentAlpha);
    m_battery.render(ren);

    const nxui::Rect actionRect = {390.f, 584.f + lift, 500.f, 82.f};
    m_actionPanel.setRect(actionRect);
    m_actionPanel.setOpacity(contentAlpha);
    m_actionPanel.render(ren);
    drawGradientRoundedOutline(
        ren,
        actionRect.shrunk(1.f),
        35.f,
        1.25f,
        0.22f * contentAlpha,
        std::fmod(m_pulse * 0.045f + 0.18f, 1.f)
    );

    if (m_fontMedium && m_fontIcons) {
        const std::string instruction = "Appuie 3 fois sur";
        const std::string aGlyph = utf8Codepoint(0xE0E0);
        const float textScale = 0.84f;
        const float glyphScale = 1.30f;
        const nxui::Vec2 textSize = m_fontMedium->measure(instruction);
        const nxui::Vec2 glyphSize = m_fontIcons->measure(aGlyph);
        const float progressWidth = 120.f;
        const float gapTextToGlyph = 16.f;
        const float gapGlyphToProgress = 22.f;
        const float totalWidth = textSize.x * textScale + gapTextToGlyph +
                                 glyphSize.x * glyphScale + gapGlyphToProgress +
                                 progressWidth;
        const float startX = actionRect.x + (actionRect.width - totalWidth) * 0.5f;
        const float centerY = actionRect.y + actionRect.height * 0.5f;
        const float textY = centerY - textSize.y * textScale * 0.5f;
        const float glyphX = startX + textSize.x * textScale + gapTextToGlyph;
        const float glyphY = centerY - glyphSize.y * glyphScale * 0.5f;

        ren.drawText(instruction, {startX, textY}, m_fontMedium,
                     nxui::Color(0.96f, 0.98f, 1.f, 0.98f * contentAlpha),
                     textScale);
        ren.drawText(aGlyph, {glyphX, glyphY}, m_fontIcons,
                     nxui::Color(0.96f, 0.98f, 1.f, 0.98f * contentAlpha),
                     glyphScale);

        m_progress.setRect({glyphX + glyphSize.x * glyphScale + gapGlyphToProgress,
                            actionRect.y + 12.f,
                            progressWidth,
                            actionRect.height - 24.f});
        m_progress.setOpacity(contentAlpha);
        m_progress.render(ren);
    }

    if (m_unlocking) {
        const float flash = std::sin(std::min(1.f, m_unlockProgress * 1.28f) * kPi);
        const nxui::Vec2 waveCenter = {
            actionRect.x + actionRect.width * 0.5f,
            actionRect.y + actionRect.height * 0.5f
        };
        const float radius = 28.f + 430.f * easeOutCubic(m_unlockProgress);
        drawArc(ren, waveCenter, radius, 0.f, 2.f * kPi,
                nxui::Color(0.72f, 0.90f, 1.f, 0.28f * flash * m_viewOpacity),
                2.3f, 112);

        if (m_unlockProgress > 0.12f && m_unlockProgress < 0.88f) {
            ren.captureToOffscreen();
            ren.applyWave(m_pulse,
                          0.006f + 0.010f * flash,
                          22.f);
        }
    }
}
