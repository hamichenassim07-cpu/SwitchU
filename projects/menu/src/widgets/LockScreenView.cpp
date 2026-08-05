#include "LockScreenView.hpp"

#include <nxui/core/Renderer.hpp>

#include <switch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
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

float smoothStep(float value) {
    value = clamp01(value);
    return value * value * (3.f - 2.f * value);
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
    const nxui::Color cyan(0.05f, 0.82f, 1.00f, 1.f);
    const nxui::Color violet(0.42f, 0.22f, 1.00f, 1.f);
    const nxui::Color pink(1.00f, 0.22f, 0.80f, 1.f);
    t = clamp01(t);
    if (t < 0.52f)
        return mixColor(cyan, violet, t / 0.52f);
    return mixColor(violet, pink, (t - 0.52f) / 0.48f);
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

std::string truncateUtf8(const std::string& text, std::size_t limit) {
    const auto glyphs = splitUtf8(text);
    std::string out;
    for (std::size_t i = 0; i < std::min(limit, glyphs.size()); ++i)
        out += glyphs[i];
    if (glyphs.size() > limit)
        out += "…";
    return out;
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
                      local->tm_hour >= 12 ? "PM" : "AM");
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
    std::snprintf(buffer, sizeof(buffer), "%s %d %s",
                  days[weekday], local->tm_mday, months[month]);
    dateText = buffer;
}

void drawArc(nxui::Renderer& ren,
             const nxui::Vec2& center,
             float radius,
             float startAngle,
             float endAngle,
             const nxui::Color& color,
             float thickness,
             int segments) {
    segments = std::max(8, segments);
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

bool pathExists(const std::string& path) {
    if (path.empty())
        return false;
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string lockscreenBasePath() {
#ifdef SWITCHU_HOMEBREW
    return "romfs:/lockscreen";
#else
    return "sdmc:/switch/SwitchU/lockscreen";
#endif
}

std::string titleIdHex(std::uint64_t titleId) {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(titleId));
    return buffer;
}

int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool hexToAccountUid(const std::string& text, AccountUid& out) {
    if (text.size() != 32)
        return false;

    AccountUid uid{};
    for (int part = 0; part < 2; ++part) {
        std::uint64_t value = 0;
        for (int i = 0; i < 16; ++i) {
            const int nibble = hexNibble(text[static_cast<std::size_t>(part * 16 + i)]);
            if (nibble < 0)
                return false;
            value = (value << 4) | static_cast<std::uint64_t>(nibble);
        }
        uid.uid[part] = value;
    }

    if (!accountUidIsValid(&uid))
        return false;
    out = uid;
    return true;
}

std::string trimProfileName(std::string name) {
    while (!name.empty() &&
           (name.front() == ' ' || name.front() == '\t'))
        name.erase(name.begin());
    while (!name.empty() &&
           (name.back() == ' ' || name.back() == '\t' ||
            name.back() == '.' || name.back() == '?' || name.back() == '!'))
        name.pop_back();
    if (name.size() > 20)
        name.clear();
    return name;
}

std::string profileHintFromGreeting(const std::string& greeting) {
    const std::size_t comma = greeting.rfind(',');
    if (comma == std::string::npos || comma + 1 >= greeting.size())
        return {};
    return trimProfileName(greeting.substr(comma + 1));
}

void drawTextureCover(nxui::Renderer& ren,
                      const nxui::Texture* texture,
                      const nxui::Rect& area,
                      float alpha) {
    if (!texture || !texture->valid() || texture->width() <= 0 || texture->height() <= 0)
        return;

    const float texW = static_cast<float>(texture->width());
    const float texH = static_cast<float>(texture->height());
    const float scale = std::max(area.width / texW, area.height / texH);
    const float drawW = texW * scale;
    const float drawH = texH * scale;
    const nxui::Rect dest = {
        area.x + (area.width - drawW) * 0.5f,
        area.y + (area.height - drawH) * 0.5f,
        drawW,
        drawH
    };
    ren.drawTexture(texture, dest, nxui::Color::white().withAlpha(alpha));
}

void drawGradientText(nxui::Renderer& ren,
                      const std::string& text,
                      nxui::Font* font,
                      const nxui::Vec2& center,
                      float scale,
                      float alpha) {
    if (!font || text.empty())
        return;

    const auto glyphs = splitUtf8(text);
    float totalWidth = 0.f;
    for (const auto& glyph : glyphs)
        totalWidth += font->measure(glyph).x * scale;

    float cursorX = center.x - totalWidth * 0.5f;
    const float denom = std::max(1.f, static_cast<float>(glyphs.size() - 1));
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const float t = static_cast<float>(i) / denom;
        const nxui::Color color = lockGradient(t).withAlpha(0.98f * alpha);
        ren.drawText(glyphs[i], {cursorX, center.y}, font, color, scale);
        cursorX += font->measure(glyphs[i]).x * scale;
    }
}

void drawBattery(nxui::Renderer& ren,
                 nxui::Font* font,
                 const nxui::Vec2& origin,
                 std::uint32_t percentage,
                 bool charging,
                 float pulse,
                 float alpha) {
    const float level = std::clamp(static_cast<float>(percentage) / 100.f, 0.f, 1.f);
    const nxui::Rect body = {origin.x, origin.y, 43.f, 21.f};
    const nxui::Color edge = nxui::Color(0.96f, 0.98f, 1.f, 0.88f * alpha);

    ren.drawRoundedRectOutline(body, edge, 5.f, 1.5f);
    ren.drawRoundedRect({body.right() + 2.f, body.y + 6.f, 4.f, 9.f},
                        edge.withAlpha(0.74f * alpha), 1.8f);

    nxui::Rect fill = body.shrunk(3.f);
    fill.width *= level;
    if (fill.width > 0.5f) {
        nxui::Color fillColor = percentage <= 20
            ? nxui::Color(1.f, 0.26f, 0.24f, 0.94f * alpha)
            : nxui::Color(0.94f, 0.97f, 1.f,
                          (charging ? 0.72f + 0.28f * pulse : 0.94f) * alpha);
        ren.drawRoundedRect(fill, fillColor,
                            std::min(3.5f, fill.width * 0.5f));
    }

    if (font) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%u %%", static_cast<unsigned>(percentage));
        ren.drawText(buffer, {origin.x + 58.f, origin.y - 2.f}, font,
                     nxui::Color(0.96f, 0.98f, 1.f, 0.94f * alpha), 0.78f);
    }

    if (charging) {
        const float x = origin.x + 47.f;
        const float y = origin.y + 2.f;
        ren.drawLine({x + 4.f, y}, {x, y + 8.f},
                     nxui::Color(0.42f, 1.f, 0.68f, alpha), 2.f);
        ren.drawLine({x, y + 8.f}, {x + 5.f, y + 8.f},
                     nxui::Color(0.42f, 1.f, 0.68f, alpha), 2.f);
        ren.drawLine({x + 5.f, y + 8.f}, {x + 1.f, y + 17.f},
                     nxui::Color(0.42f, 1.f, 0.68f, alpha), 2.f);
    }
}

} // namespace

LockScreenView::LockScreenView() {
    setRect({0.f, 0.f, 1280.f, 720.f});

    for (nxui::GlassPanel* panel : {&m_profilePanel, &m_sessionPanel}) {
        panel->setForceLiquidGlass(true);
        panel->setBlurEnabled(false);
        panel->setBackingEnabled(true);
        panel->setBackingColor({0.008f, 0.010f, 0.032f, 1.f});
        panel->setMaterialTextureEnabled(true);
        panel->setMaterialTextureIntensity(0.36f);
    }
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
}

void LockScreenView::setTheme(const nxui::Theme* theme) {
    m_theme = theme;
    m_progress.setTheme(theme);
    applyThemeToWidgets();
}

void LockScreenView::setSuspendedGame(nxui::Texture* texture,
                                      const std::string& title,
                                      std::uint64_t titleId,
                                      bool gameCard) {
    const bool titleChanged = m_gameTitleId != titleId;
    m_gameTexture = texture;
    m_gameTitle = title;
    m_gameTitleId = titleId;
    m_gameIsCard = gameCard;
    m_hasGame = titleId != 0;

    if (titleChanged)
        resetBackgroundAsset();
}

void LockScreenView::clearSuspendedGame() {
    m_gameTexture = nullptr;
    m_gameTitle.clear();
    m_gameTitleId = 0;
    m_gameIsCard = false;
    m_hasGame = false;
    resetBackgroundAsset();
}

void LockScreenView::setGreeting(const std::string& greeting) {
    m_greeting = greeting.empty() ? "Bon retour." : greeting;
    const std::string newHint = profileHintFromGreeting(m_greeting);
    if (newHint != m_profileNameHint) {
        m_profileNameHint = newHint;
        resetProfileAsset();
    }
}

void LockScreenView::setBatteryStatus(std::uint32_t percentage, bool charging) {
    m_batteryPercent = std::min<std::uint32_t>(percentage, 100u);
    m_batteryCharging = charging;
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
    const nxui::Color base = {0.025f, 0.028f, 0.090f, 0.66f};
    const nxui::Color border = {0.50f, 0.34f, 1.00f, 0.42f};
    const nxui::Color highlight = {0.90f, 0.96f, 1.00f, 0.14f};

    for (nxui::GlassPanel* panel : {&m_profilePanel, &m_sessionPanel}) {
        panel->setBaseColor(base);
        panel->setBorderColor(border);
        panel->setHighlightColor(highlight);
        panel->setBorderWidth(1.35f);
        panel->setCornerRadius(25.f);
    }
}

void LockScreenView::resetBackgroundAsset() {
    m_backgroundTexture = nxui::Texture{};
    m_backgroundPath.clear();
    m_backgroundAttempted = false;
}

void LockScreenView::resetProfileAsset() {
    m_profileAvatarTexture = nxui::Texture{};
    m_profileName.clear();
    m_profileAttempted = false;
}

void LockScreenView::ensureBackground(nxui::Renderer& ren) {
    if (m_backgroundAttempted)
        return;
    m_backgroundAttempted = true;

    const std::string base = lockscreenBasePath();
    std::vector<std::string> candidates;

    if (m_hasGame && m_gameTitleId != 0) {
        const std::string stem = base + "/backgrounds/" + titleIdHex(m_gameTitleId);
        candidates.push_back(stem + ".png");
        candidates.push_back(stem + ".jpg");
        candidates.push_back(stem + ".jpeg");
    }

    candidates.push_back(base + "/default.png");
    candidates.push_back(base + "/default.jpg");
    candidates.push_back(base + "/default.jpeg");

    for (const std::string& path : candidates) {
        if (!pathExists(path))
            continue;
        nxui::Texture loaded;
        if (loaded.loadFromFile(ren.gpu(), ren, path, 0)) {
            m_backgroundTexture = std::move(loaded);
            m_backgroundPath = path;
            break;
        }
    }
}

void LockScreenView::ensureProfile(nxui::Renderer& ren) {
    if (m_profileAttempted)
        return;
    m_profileAttempted = true;

    AccountUid uid{};
    bool uidResolved = false;

    {
        std::ifstream input("sdmc:/config/SwitchU/last_profile_uid.txt");
        std::string persisted;
        if (input && (input >> persisted))
            uidResolved = hexToAccountUid(persisted, uid);
    }

    if (!uidResolved) {
        AccountUid silent{};
        if (R_SUCCEEDED(accountTrySelectUserWithoutInteraction(&silent, false)) &&
            accountUidIsValid(&silent)) {
            uid = silent;
            uidResolved = true;
        }
    }

    if (!uidResolved) {
        AccountUid users[8] = {};
        s32 count = 0;
        if (R_SUCCEEDED(accountListAllUsers(users, 8, &count)) && count == 1) {
            uid = users[0];
            uidResolved = true;
        }
    }

    if (uidResolved) {
        AccountProfile profile{};
        if (R_SUCCEEDED(accountGetProfile(&profile, uid))) {
            AccountUserData userData{};
            AccountProfileBase base{};
            if (R_SUCCEEDED(accountProfileGet(&profile, &userData, &base)))
                m_profileName = trimProfileName(base.nickname);

            u32 imageSize = 0;
            if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imageSize)) &&
                imageSize > 0) {
                std::vector<std::uint8_t> imageData(imageSize);
                u32 realSize = 0;
                if (R_SUCCEEDED(accountProfileLoadImage(
                        &profile, imageData.data(), imageSize, &realSize)) &&
                    realSize > 0) {
                    m_profileAvatarTexture.loadFromMemory(
                        ren.gpu(), ren, imageData.data(), realSize, 256);
                }
            }
            accountProfileClose(&profile);
        }
    }

    if (m_profileName.empty())
        m_profileName = m_profileNameHint;
    if (m_profileName.empty())
        m_profileName = "Profil";
}

void LockScreenView::ensureDynamicAssets(nxui::Renderer& ren) {
    ensureBackground(ren);
    ensureProfile(ren);
}

void LockScreenView::onUpdate(float dt) {
    m_progress.update(dt);
}

void LockScreenView::onRender(nxui::Renderer& ren) {
    const float contentAlpha = m_viewOpacity * easeOutCubic(m_reveal);
    if (contentAlpha <= 0.001f)
        return;

    ensureDynamicAssets(ren);

    const float breathe = 0.5f + 0.5f * std::sin(m_pulse * 1.25f);
    const float slowPulse = 0.5f + 0.5f * std::sin(m_pulse * 0.42f);
    const float lift = -22.f * m_unlockProgress;
    const nxui::Rect screen = {0.f, 0.f, 1280.f, 720.f};

    if (m_backgroundTexture.valid()) {
        drawTextureCover(ren, &m_backgroundTexture, screen, m_viewOpacity);
    } else {
        ren.drawGradientRect(
            screen,
            nxui::Color(0.004f, 0.006f, 0.020f, m_viewOpacity),
            nxui::Color(0.030f, 0.008f, 0.060f, m_viewOpacity)
        );
        ren.drawCircle({220.f, 610.f}, 330.f,
                       nxui::Color(0.04f, 0.22f, 0.62f,
                                   (0.09f + 0.025f * slowPulse) * contentAlpha), 96);
        ren.drawCircle({1080.f, 100.f}, 320.f,
                       nxui::Color(0.42f, 0.04f, 0.54f,
                                   (0.08f + 0.025f * breathe) * contentAlpha), 96);
    }

    // The artwork remains visible, but the UI always keeps enough contrast.
    ren.drawGradientRect(
        screen,
        nxui::Color(0.004f, 0.008f, 0.025f, 0.42f * m_viewOpacity),
        nxui::Color(0.004f, 0.006f, 0.020f, 0.78f * m_viewOpacity)
    );
    ren.drawRect(screen, nxui::Color(0.002f, 0.004f, 0.014f,
                                     0.20f * m_viewOpacity));

    // Large quiet orbital lines behind the central application.
    const nxui::Vec2 center = {650.f, 395.f + lift * 0.10f};
    if (m_hasGame) {
        drawArc(ren, center, 245.f, 0.f, 2.f * kPi,
                nxui::Color(0.18f, 0.40f, 0.96f, 0.12f * contentAlpha),
                1.3f, 112);
        drawArc(ren, center, 285.f, 0.30f, 5.35f,
                nxui::Color(0.50f, 0.20f, 1.00f, 0.09f * contentAlpha),
                1.0f, 108);
    }

    // Clock, date and battery — deliberately free of a card, like the concept.
    std::string timeText;
    std::string dateText;
    buildClockStrings(m_use12Hour, timeText, dateText);

    if (m_fontLarge) {
        ren.drawText(timeText, {48.f, 34.f + lift * 0.04f}, m_fontLarge,
                     nxui::Color(0.f, 0.f, 0.f, 0.28f * contentAlpha), 1.18f);
        ren.drawText(timeText, {44.f, 30.f + lift * 0.04f}, m_fontLarge,
                     nxui::Color(0.98f, 0.99f, 1.f, contentAlpha), 1.18f);
    }
    if (m_fontMedium) {
        ren.drawText(dateText, {50.f, 145.f + lift * 0.04f}, m_fontMedium,
                     nxui::Color(0.94f, 0.96f, 1.f, 0.94f * contentAlpha), 0.72f);
    }
    drawBattery(ren, m_fontNormal, {54.f, 195.f + lift * 0.04f},
                m_batteryPercent, m_batteryCharging,
                0.72f + 0.28f * breathe, contentAlpha);

    // Adaptive greeting centered over the application.
    float greetingScale = 1.20f;
    if (m_fontMedium) {
        const float maxWidth = 610.f;
        const float measured = m_fontMedium->measure(m_greeting).x;
        if (measured > 0.f)
            greetingScale = std::clamp(maxWidth / measured, 0.82f, 1.20f);
    }
    drawGradientText(ren, m_greeting, m_fontMedium,
                     {650.f, 66.f + lift * 0.03f},
                     greetingScale, contentAlpha);

    if (m_hasGame) {
        // "JEU SUSPENDU" label.
        const nxui::Rect statusRect = {555.f, 178.f + lift * 0.08f, 190.f, 42.f};
        ren.drawRoundedRect(statusRect,
                            nxui::Color(0.018f, 0.026f, 0.090f,
                                        0.48f * contentAlpha), 21.f);
        ren.drawRoundedRectOutline(statusRect,
                                   nxui::Color(0.34f, 0.50f, 1.00f,
                                               0.30f * contentAlpha),
                                   21.f, 1.2f);
        if (m_fontSmall) {
            const std::string label = "JEU SUSPENDU";
            const nxui::Vec2 size = m_fontSmall->measure(label);
            ren.drawText(label,
                         {statusRect.x + (statusRect.width - size.x * 0.88f) * 0.5f,
                          statusRect.y + 9.f},
                         m_fontSmall,
                         nxui::Color(0.86f, 0.90f, 1.f, 0.82f * contentAlpha),
                         0.88f);
        }

        // Three large press segments around the cover.
        m_progress.setRect({418.f, 218.f + lift * 0.10f, 464.f, 392.f});
        m_progress.setOpacity(contentAlpha);
        m_progress.render(ren);

        // Central cover. The application can be known before its texture has
        // finished streaming, so a clean placeholder is provided.
        const nxui::Rect cover = {558.f, 305.f + lift * 0.10f, 184.f, 184.f};
        ren.drawRoundedRect(cover.expanded(13.f),
                            nxui::Color(0.08f, 0.30f, 0.86f,
                                        (0.09f + 0.04f * breathe) * contentAlpha),
                            34.f);
        ren.drawRoundedRect(cover,
                            nxui::Color(0.018f, 0.022f, 0.070f,
                                        0.96f * contentAlpha), 28.f);
        if (m_gameTexture && m_gameTexture->valid()) {
            ren.drawTextureRounded(m_gameTexture, cover.shrunk(6.f), 23.f,
                                   nxui::Color::white().withAlpha(contentAlpha));
        } else {
            ren.drawGradientRect(cover.shrunk(7.f),
                                 nxui::Color(0.16f, 0.28f, 0.62f, 0.76f * contentAlpha),
                                 nxui::Color(0.26f, 0.08f, 0.46f, 0.78f * contentAlpha));
        }
        ren.drawRoundedRectOutline(cover,
                                   nxui::Color(0.72f, 0.86f, 1.f,
                                               0.56f * contentAlpha),
                                   28.f, 1.6f);
    }

    // Profile card.
    const nxui::Rect profileRect = {952.f, 252.f + lift * 0.06f, 292.f, 164.f};
    m_profilePanel.setRect(profileRect);
    m_profilePanel.setOpacity(contentAlpha);
    m_profilePanel.render(ren);

    if (m_fontSmall) {
        ren.drawText("PROFIL ACTIF",
                     {profileRect.x + 24.f, profileRect.y + 17.f},
                     m_fontSmall,
                     nxui::Color(0.85f, 0.88f, 0.98f, 0.82f * contentAlpha),
                     0.92f);
    }
    ren.drawRect({profileRect.x + 24.f, profileRect.y + 54.f,
                  profileRect.width - 48.f, 1.f},
                 nxui::Color(0.78f, 0.84f, 1.f, 0.22f * contentAlpha));

    const nxui::Rect avatarRect = {
        profileRect.x + 25.f,
        profileRect.y + 72.f,
        66.f,
        66.f
    };
    if (m_profileAvatarTexture.valid()) {
        ren.drawTextureRounded(&m_profileAvatarTexture, avatarRect, 33.f,
                               nxui::Color::white().withAlpha(contentAlpha));
    } else {
        ren.drawCircle({avatarRect.x + 33.f, avatarRect.y + 33.f}, 33.f,
                       lockGradient(0.14f).withAlpha(0.90f * contentAlpha), 42);
        if (m_fontMedium && !m_profileName.empty()) {
            const std::string initial = splitUtf8(m_profileName).front();
            const nxui::Vec2 size = m_fontMedium->measure(initial);
            ren.drawText(initial,
                         {avatarRect.x + (avatarRect.width - size.x * 0.90f) * 0.5f,
                          avatarRect.y + 15.f},
                         m_fontMedium,
                         nxui::Color(1.f, 1.f, 1.f, contentAlpha), 0.90f);
        }
    }
    if (m_fontMedium) {
        ren.drawText(truncateUtf8(m_profileName, 14),
                     {profileRect.x + 113.f, profileRect.y + 89.f},
                     m_fontMedium,
                     nxui::Color(0.98f, 0.99f, 1.f, 0.98f * contentAlpha),
                     0.84f);
    }

    // Session card only exists when an application is actually suspended.
    if (m_hasGame) {
        const nxui::Rect sessionRect = {952.f, 458.f + lift * 0.06f, 292.f, 174.f};
        m_sessionPanel.setRect(sessionRect);
        m_sessionPanel.setOpacity(contentAlpha);
        m_sessionPanel.render(ren);

        if (m_fontSmall) {
            ren.drawText("SESSION EN COURS",
                         {sessionRect.x + 24.f, sessionRect.y + 16.f},
                         m_fontSmall,
                         nxui::Color(0.85f, 0.88f, 0.98f, 0.82f * contentAlpha),
                         0.92f);
        }
        ren.drawRect({sessionRect.x + 24.f, sessionRect.y + 54.f,
                      sessionRect.width - 48.f, 1.f},
                     nxui::Color(0.78f, 0.84f, 1.f, 0.22f * contentAlpha));

        const nxui::Rect miniCover = {
            sessionRect.x + 24.f,
            sessionRect.y + 73.f,
            72.f,
            72.f
        };
        ren.drawRoundedRect(miniCover,
                            nxui::Color(0.025f, 0.030f, 0.090f,
                                        0.92f * contentAlpha), 14.f);
        if (m_gameTexture && m_gameTexture->valid()) {
            ren.drawTextureRounded(m_gameTexture, miniCover.shrunk(3.f), 12.f,
                                   nxui::Color::white().withAlpha(contentAlpha));
        }
        ren.drawRoundedRectOutline(miniCover,
                                   nxui::Color(0.58f, 0.72f, 1.f,
                                               0.42f * contentAlpha),
                                   14.f, 1.2f);

        if (m_fontNormal) {
            ren.drawText(truncateUtf8(m_gameTitle, 19),
                         {sessionRect.x + 114.f, sessionRect.y + 80.f},
                         m_fontNormal,
                         nxui::Color(0.98f, 0.99f, 1.f, 0.96f * contentAlpha),
                         0.72f);
        }
        if (m_fontSmall) {
            ren.drawText("Jeu suspendu",
                         {sessionRect.x + 114.f, sessionRect.y + 119.f},
                         m_fontSmall,
                         nxui::Color(0.72f, 0.76f, 0.90f, 0.72f * contentAlpha),
                         0.86f);
        }
    }

    // Bottom instruction. The ring is now the progress indicator, so the old
    // three dots are intentionally removed.
    if (m_fontMedium && m_fontIcons) {
        const std::string instruction = "Appuie trois fois sur";
        const std::string aGlyph = utf8Codepoint(0xE0E0);
        const float textScale = 0.78f;
        const float glyphScale = 1.22f;
        const nxui::Vec2 textSize = m_fontMedium->measure(instruction);
        const nxui::Vec2 glyphSize = m_fontIcons->measure(aGlyph);
        const float gap = 15.f;
        const float totalW = textSize.x * textScale + gap + glyphSize.x * glyphScale;
        const float startX = 650.f - totalW * 0.5f;
        const float y = 650.f + lift;

        ren.drawText(instruction, {startX, y}, m_fontMedium,
                     nxui::Color(0.97f, 0.98f, 1.f, 0.95f * contentAlpha),
                     textScale);
        const float glyphX = startX + textSize.x * textScale + gap;
        ren.drawCircle({glyphX + glyphSize.x * glyphScale * 0.5f,
                        y + glyphSize.y * glyphScale * 0.47f},
                       25.f + 2.f * breathe,
                       nxui::Color(0.48f, 0.72f, 1.f,
                                   (0.05f + 0.035f * breathe) * contentAlpha), 32);
        ren.drawText(aGlyph, {glyphX, y - 3.f}, m_fontIcons,
                     nxui::Color(0.98f, 0.99f, 1.f, contentAlpha),
                     glyphScale);
    }

    if (m_unlocking) {
        const float flash = std::sin(std::min(1.f, m_unlockProgress * 1.28f) * kPi);
        const float eased = easeOutCubic(m_unlockProgress);
        const float radius = 34.f + 500.f * eased;
        drawArc(ren, center, radius, 0.f, 2.f * kPi,
                nxui::Color(0.70f, 0.90f, 1.f,
                            0.30f * flash * m_viewOpacity),
                2.4f, 128);

        if (m_unlockProgress > 0.10f && m_unlockProgress < 0.88f) {
            ren.captureToOffscreen();
            ren.applyWave(m_pulse,
                          0.005f + 0.010f * flash,
                          22.f);
        }

        // Brief white veil at the strongest point of the transition.
        const float veil = 0.08f * smoothStep(flash);
        ren.drawRect(screen, nxui::Color(0.88f, 0.95f, 1.f,
                                         veil * m_viewOpacity));
    }
}
