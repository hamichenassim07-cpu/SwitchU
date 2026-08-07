#include "LockScreenView.hpp"
#include "launcher/AppListLoader.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "core/AudioManager.hpp"
#include "core/DebugLog.hpp"
#include <nxui/Application.hpp>
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif

#include <nxui/core/Renderer.hpp>

#include <switch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <utility>

std::atomic<bool> LockScreenView::s_visiblyActive{false};

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

nxui::Color animatedLockGradient(float t, float phase) {
    // Cyclic cyan -> violet -> pink -> cyan palette. A cyclic palette avoids
    // a visible colour jump when the moving gradient loops.
    const nxui::Color cyan(0.05f, 0.82f, 1.00f, 1.f);
    const nxui::Color violet(0.42f, 0.22f, 1.00f, 1.f);
    const nxui::Color pink(1.00f, 0.22f, 0.80f, 1.f);
    float sample = std::fmod(t - phase, 1.f);
    if (sample < 0.f)
        sample += 1.f;
    if (sample < 0.34f)
        return mixColor(cyan, violet, sample / 0.34f);
    if (sample < 0.68f)
        return mixColor(violet, pink, (sample - 0.34f) / 0.34f);
    return mixColor(pink, cyan, (sample - 0.68f) / 0.32f);
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

std::string truncateUtf8ToWidth(const std::string& text,
                                nxui::Font* font,
                                float scale,
                                float maxWidth) {
    if (!font || text.empty() || maxWidth <= 0.f)
        return {};
    if (font->measure(text).x * scale <= maxWidth)
        return text;

    const auto glyphs = splitUtf8(text);
    const std::string ellipsis = "…";
    std::string out;
    for (const auto& glyph : glyphs) {
        const std::string candidate = out + glyph + ellipsis;
        if (font->measure(candidate).x * scale > maxWidth)
            break;
        out += glyph;
    }
    return out.empty() ? ellipsis : out + ellipsis;
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

struct ClockStrings {
    std::string hour;
    std::string minute;
    std::string suffix;
    std::string date;
    bool separatorVisible = true;
};

void buildClockStrings(bool use12Hour, ClockStrings& out) {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) {
        out.hour = "--";
        out.minute = "--";
        out.suffix.clear();
        out.date = "Date indisponible";
        out.separatorVisible = true;
        return;
    }

    char buffer[96] = {};
    int hour = local->tm_hour;
    if (use12Hour) {
        hour %= 12;
        if (hour == 0) hour = 12;
        out.suffix = local->tm_hour >= 12 ? "PM" : "AM";
    } else {
        out.suffix.clear();
    }

    std::snprintf(buffer, sizeof(buffer), use12Hour ? "%d" : "%02d", hour);
    out.hour = buffer;
    std::snprintf(buffer, sizeof(buffer), "%02d", local->tm_min);
    out.minute = buffer;

    // Brutal, alarm-clock-style blink: no interpolation or fading.
    // V6.2 is slightly faster than one second per state.
    const auto blinkMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    out.separatorVisible = ((blinkMs / 800) % 2) == 0;

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
                  days[weekday], local->tm_mday, months[month],
                  local->tm_year + 1900);
    out.date = buffer;
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

std::string cleanDeviceName(std::string name) {
    while (!name.empty() &&
           (name.front() == ' ' || name.front() == '\t' ||
            name.front() == '\r' || name.front() == '\n'))
        name.erase(name.begin());
    while (!name.empty() &&
           (name.back() == ' ' || name.back() == '\t' ||
            name.back() == '\r' || name.back() == '\n'))
        name.pop_back();
    return truncateUtf8(name, 34);
}

std::string lowercaseAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
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

void drawReadableText(nxui::Renderer& ren,
                      const std::string& text,
                      const nxui::Vec2& pos,
                      nxui::Font* font,
                      const nxui::Color& color,
                      float scale,
                      float alpha,
                      float weight = 0.f) {
    if (!font || text.empty())
        return;

    const nxui::Color shadow(0.002f, 0.003f, 0.012f, 0.78f * alpha);
    ren.drawText(text, {pos.x + 2.2f, pos.y + 2.4f}, font, shadow, scale);
    ren.drawText(text, {pos.x - 1.0f, pos.y + 0.4f}, font,
                 shadow.withAlpha(0.46f * alpha), scale);
    ren.drawText(text, pos, font, color.withAlpha(color.a * alpha), scale);
    if (weight > 0.f) {
        ren.drawText(text, {pos.x + weight, pos.y}, font,
                     color.withAlpha(color.a * 0.78f * alpha), scale);
    }
}


void drawAutoScrollText(nxui::Renderer& ren,
                        const std::string& text,
                        const nxui::Rect& clip,
                        nxui::Font* font,
                        const nxui::Color& color,
                        float scale,
                        float alpha,
                        float animationTime,
                        float phase = 0.f,
                        bool centerWhenFits = false,
                        float weight = 0.f) {
    if (!font || text.empty() || clip.width <= 1.f || clip.height <= 1.f)
        return;

    const float textWidth = font->measure(text).x * scale;
    float x = clip.x;
    if (textWidth <= clip.width) {
        if (centerWhenFits)
            x = clip.x + (clip.width - textWidth) * 0.5f;
        drawReadableText(ren, text, {x, clip.y}, font, color,
                         scale, alpha, weight);
        return;
    }

    // Calm marquee: pause at the beginning, scroll to the end, pause,
    // then return quickly. It is only enabled when the text really overflows.
    const float overflow = textWidth - clip.width;
    const float startPause = 1.65f;
    const float travelDuration = std::max(1.20f, overflow / 42.f);
    const float endPause = 0.90f;
    const float returnDuration = 0.48f;
    const float cycle = startPause + travelDuration + endPause + returnDuration;
    float t = std::fmod(std::max(0.f, animationTime + phase), cycle);
    float offset = 0.f;

    if (t < startPause) {
        offset = 0.f;
    } else if (t < startPause + travelDuration) {
        const float u = (t - startPause) / travelDuration;
        offset = overflow * smoothStep(u);
    } else if (t < startPause + travelDuration + endPause) {
        offset = overflow;
    } else {
        const float u = (t - startPause - travelDuration - endPause) /
                        returnDuration;
        offset = overflow * (1.f - smoothStep(u));
    }

    ren.pushClipRect(clip);
    drawReadableText(ren, text, {clip.x - offset, clip.y}, font, color,
                     scale, alpha, weight);
    ren.popClipRect();
}

std::string formatStorageAmount(std::uint64_t bytes) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    const double value = static_cast<double>(bytes) / kGiB;
    char buffer[48] = {};
    if (value < 100.0)
        std::snprintf(buffer, sizeof(buffer), "%.1f Go", value);
    else
        std::snprintf(buffer, sizeof(buffer), "%.0f Go", value);
    return buffer;
}

void drawGradientText(nxui::Renderer& ren,
                      const std::string& text,
                      nxui::Font* font,
                      const nxui::Vec2& center,
                      float scale,
                      float alpha,
                      float animationTime) {
    if (!font || text.empty())
        return;

    const auto glyphs = splitUtf8(text);
    float totalWidth = 0.f;
    for (const auto& glyph : glyphs)
        totalWidth += font->measure(glyph).x * scale;

    float cursorX = center.x - totalWidth * 0.5f;
    const float denom = std::max(1.f, static_cast<float>(glyphs.size() - 1));
    const float phase = std::fmod(animationTime / 3.6f, 1.f);
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const float t = static_cast<float>(i) / denom;
        const nxui::Color color =
            animatedLockGradient(t, phase).withAlpha(0.99f * alpha);
        const nxui::Color outline(0.002f, 0.004f, 0.014f, 0.56f * alpha);
        ren.drawText(glyphs[i], {cursorX - 1.15f, center.y + 0.35f},
                     font, outline, scale);
        ren.drawText(glyphs[i], {cursorX + 1.15f, center.y + 0.35f},
                     font, outline, scale);
        ren.drawText(glyphs[i], {cursorX, center.y + 1.35f},
                     font, outline, scale);
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
    const nxui::Rect body = {origin.x, origin.y, 50.f, 24.f};
    const nxui::Color edge(0.97f, 0.985f, 1.f, 0.94f * alpha);

    ren.drawRoundedRectOutline(body, edge, 5.8f, 1.65f);
    ren.drawRoundedRect({body.right() + 2.2f, body.y + 6.5f, 4.5f, 11.f},
                        edge.withAlpha(0.82f * alpha), 1.8f);

    nxui::Rect fill = body.shrunk(3.4f);
    fill.width *= level;
    if (fill.width > 0.5f) {
        nxui::Color fillColor;
        if (charging) {
            fillColor = nxui::Color(0.22f, 1.00f, 0.48f,
                                    (0.54f + 0.43f * pulse) * alpha);
        } else if (percentage <= 20) {
            fillColor = nxui::Color(1.f, 0.26f, 0.24f, 0.96f * alpha);
        } else {
            fillColor = nxui::Color(0.95f, 0.98f, 1.f, 0.96f * alpha);
        }
        ren.drawRoundedRect(fill, fillColor,
                            std::min(4.0f, fill.width * 0.5f));
        if (charging) {
            ren.drawRoundedRect(fill.expanded(1.0f),
                                nxui::Color(0.22f, 1.f, 0.48f,
                                            (0.020f + 0.050f * pulse) * alpha),
                                std::min(4.8f, fill.width * 0.5f));
        }
    }

    if (font) {
        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%u%%", static_cast<unsigned>(percentage));
        drawReadableText(ren, buffer,
                         {body.right() + 13.f, origin.y + 0.9f},
                         font,
                         nxui::Color(0.97f, 0.985f, 1.f, 0.98f),
                         0.96f, alpha, 0.38f);
    }
}

struct FloatingShapeSpec {
    float baseX;
    float baseY;
    float size;
    float speed;
    float wobble;
    float phase;
    int type;
};

void drawFloatingShapes(nxui::Renderer& ren,
                        float animationTime,
                        float alpha,
                        bool strongMode) {
    static constexpr std::array<FloatingShapeSpec, 15> shapes = {{
        {90.f,  650.f, 22.f, 10.f, 13.f, 0.2f, 0},
        {210.f, 450.f, 14.f, 14.f, 18.f, 1.4f, 2},
        {330.f, 690.f, 18.f,  8.f, 12.f, 2.7f, 1},
        {420.f, 270.f, 12.f, 11.f, 10.f, 4.2f, 3},
        {865.f, 650.f, 19.f,  9.f, 14.f, 0.9f, 2},
        {995.f, 520.f, 13.f, 13.f, 16.f, 2.1f, 0},
        {1170.f,690.f, 24.f,  7.f, 11.f, 3.5f, 1},
        {1210.f,350.f, 11.f, 12.f, 15.f, 5.1f, 3},
        {740.f, 170.f, 10.f,  8.f,  9.f, 1.9f, 0},
        {540.f, 690.f, 13.f, 10.f, 13.f, 3.9f, 2},
        {1040.f,160.f, 16.f,  6.f, 10.f, 4.8f, 1},
        {610.f, 610.f, 18.f,  9.f, 14.f, 0.6f, 3},
        {790.f, 510.f, 13.f, 12.f, 16.f, 2.9f, 1},
        {150.f, 235.f, 16.f,  7.f, 11.f, 4.5f, 2},
        {1125.f,580.f, 15.f, 10.f, 13.f, 5.6f, 0},
    }};

    const float sizeScale = strongMode ? 1.30f : 1.f;
    const float bodyBaseAlpha = strongMode ? 0.105f : 0.030f;
    const float bodyShimmerAlpha = strongMode ? 0.070f : 0.026f;
    const float glowBaseAlpha = strongMode ? 0.030f : 0.010f;

    for (std::size_t i = 0; i < shapes.size(); ++i) {
        const auto& shape = shapes[i];
        float wrappedY = std::fmod(
            shape.baseY - animationTime * shape.speed + 820.f, 820.f);
        if (wrappedY < 0.f)
            wrappedY += 820.f;
        const float y = wrappedY - 50.f;
        const float x = shape.baseX +
            std::sin(animationTime * 0.52f + shape.phase) * shape.wobble;
        const float rotation = animationTime * (0.12f + 0.025f * static_cast<float>(i)) +
                               shape.phase;
        const float shimmer = 0.72f + 0.28f *
            std::sin(animationTime * 0.7f + shape.phase);
        const float size = shape.size * sizeScale;
        const nxui::Color baseColor = lockGradient(
            static_cast<float>(i % 3) / 2.f
        );
        const nxui::Color c = baseColor.withAlpha(
            (bodyBaseAlpha + bodyShimmerAlpha * shimmer) * alpha);
        const nxui::Color glow = baseColor.withAlpha(
            (glowBaseAlpha + 0.018f * shimmer) * alpha);

        if (strongMode) {
            if (shape.type == 0) {
                ren.drawCircle({x, y}, size * 1.34f, glow, 30);
            } else {
                ren.drawCircle({x, y}, size * 1.42f,
                               glow.withAlpha(glow.a * 0.72f), 28);
            }
        }

        switch (shape.type) {
            case 0:
                ren.drawCircle({x, y}, size, c, 28);
                ren.drawCircle({x - size * 0.18f, y - size * 0.22f},
                               size * 0.44f,
                               nxui::Color(1.f, 1.f, 1.f, (strongMode ? 0.040f : 0.018f) * alpha), 20);
                break;
            case 1: {
                const float cs = std::cos(rotation);
                const float sn = std::sin(rotation);
                const nxui::Vec2 p0{x + cs * size, y + sn * size};
                const nxui::Vec2 p1{x + std::cos(rotation + 2.094f) * size,
                                    y + std::sin(rotation + 2.094f) * size};
                const nxui::Vec2 p2{x + std::cos(rotation + 4.188f) * size,
                                    y + std::sin(rotation + 4.188f) * size};
                ren.drawTriangle(p0, p1, p2, c);
                break;
            }
            case 2:
                ren.drawRoundedRect({x - size, y - size,
                                     size * 2.f, size * 2.f},
                                    c, size * 0.28f);
                break;
            default: {
                const nxui::Vec2 top{x, y - size};
                const nxui::Vec2 right{x + size, y};
                const nxui::Vec2 bottom{x, y + size};
                const nxui::Vec2 left{x - size, y};
                ren.drawTriangle(top, right, bottom, c);
                ren.drawTriangle(top, bottom, left, c);
                break;
            }
        }
    }
}

void drawNoGameUnlockPill(nxui::Renderer& ren,
                          nxui::Font* textFont,
                          nxui::Font* iconFont,
                          int progress,
                          float flash,
                          float pulse,
                          float alpha,
                          float lift) {
    if (!textFont || !iconFont)
        return;

    const float breathe = 0.5f + 0.5f * std::sin(pulse * 2.05f);
    const nxui::Rect pill = {414.f, 582.f + lift, 472.f, 104.f};
    ren.drawRoundedRect(pill,
                        nxui::Color(0.010f, 0.014f, 0.052f, 0.72f * alpha),
                        35.f);
    ren.drawRoundedRectOutline(pill,
                               nxui::Color(0.48f, 0.34f, 1.f,
                                           (0.48f + 0.08f * breathe) * alpha),
                               35.f, 1.8f);

    const std::string instruction = "Appuie trois fois sur";
    const std::string aGlyph = utf8Codepoint(0xE0E0);
    const float textScale = 0.82f;
    const float pressKick = smoothStep(flash);
    const float baseGlyphScale = 1.24f;
    const float glyphScale = baseGlyphScale * (1.f - 0.10f * pressKick);
    const nxui::Vec2 textSize = textFont->measure(instruction);
    const nxui::Vec2 glyphSize = iconFont->measure(aGlyph);
    const float gap = 14.f;
    const float totalW = textSize.x * textScale + gap +
                         glyphSize.x * baseGlyphScale;
    const float startX = pill.x + (pill.width - totalW) * 0.5f;
    const float y = pill.y + 14.f;

    drawReadableText(ren, instruction, {startX, y}, textFont,
                     nxui::Color(0.98f, 0.99f, 1.f, 0.98f),
                     textScale, alpha, 0.35f);
    const float glyphX = startX + textSize.x * textScale + gap;
    const nxui::Vec2 glyphCenter = {
        glyphX + glyphSize.x * baseGlyphScale * 0.5f,
        y + glyphSize.y * baseGlyphScale * 0.46f
    };
    // The A glyph uses a compact neutral-white glow so it no longer competes
    // with the blue progress accents.
    ren.drawCircle(glyphCenter,
                   21.f + 5.f * pressKick + 1.2f * breathe,
                   nxui::Color(1.f, 1.f, 1.f,
                       (0.045f + 0.11f * pressKick +
                        0.018f * breathe) * alpha), 32);
    ren.drawCircle(glyphCenter,
                   16.f + 2.f * pressKick,
                   nxui::Color(0.92f, 0.97f, 1.f,
                               (0.025f + 0.055f * pressKick) * alpha), 28);
    ren.drawText(aGlyph,
                 {glyphCenter.x - glyphSize.x * glyphScale * 0.5f,
                  glyphCenter.y - glyphSize.y * glyphScale * 0.46f - 3.f},
                 iconFont,
                 nxui::Color(0.995f, 1.f, 1.f, alpha),
                 glyphScale);

    const float dotY = pill.y + 77.f;
    const float spacing = 31.f;
    for (int i = 0; i < 3; ++i) {
        const float x = pill.x + pill.width * 0.5f + (i - 1) * spacing;
        const bool active = i < progress;
        const bool next = i == progress && progress < 3;
        const float localFlash = active && i == progress - 1
            ? std::clamp(flash, 0.f, 1.f)
            : 0.f;

        if (active) {
            const nxui::Color c = lockGradient(static_cast<float>(i) / 2.f);
            ren.drawCircle({x, dotY}, 17.f + 3.f * localFlash,
                           c.withAlpha((0.10f + 0.08f * localFlash) * alpha), 28);
            ren.drawCircle({x, dotY}, 8.2f + 0.9f * localFlash,
                           c.withAlpha(0.98f * alpha), 24);
        } else {
            const float nextScale = next ? (1.f + 0.18f * breathe) : 1.f;
            if (next) {
                ren.drawCircle({x, dotY}, 15.f + 2.f * breathe,
                               nxui::Color(0.45f, 0.58f, 1.f,
                                           (0.055f + 0.045f * breathe) * alpha), 28);
            }
            ren.drawCircle({x, dotY}, 6.8f * nextScale,
                           nxui::Color(0.58f, 0.64f, 0.84f,
                                       (0.34f + (next ? 0.14f * breathe : 0.f)) * alpha), 22);
        }
    }
}

} // namespace

LockScreenView::LockScreenView() {
    DebugLog::log("[lockscreen] Switch U V6.6 view created");
    setRect({0.f, 0.f, 1280.f, 720.f});

    // Compact blurred identity card in the lower-left corner.
    m_profilePanel.setForceLiquidGlass(false);
    m_profilePanel.setLiquidGlassEnabled(false);
    m_profilePanel.setBlurEnabled(true);
    m_profilePanel.setBlurRadius(1.45f);
    m_profilePanel.setBlurPasses(1);
    m_profilePanel.setBackingEnabled(false);
    m_profilePanel.setPanelOpacity(0.88f);
    m_profilePanel.setMaterialTextureEnabled(true);
    m_profilePanel.setMaterialTextureIntensity(0.08f);

    m_connectionPanel.setForceLiquidGlass(false);
    m_connectionPanel.setLiquidGlassEnabled(false);
    m_connectionPanel.setBlurEnabled(true);
    m_connectionPanel.setBlurRadius(1.75f);
    m_connectionPanel.setBlurPasses(1);
    m_connectionPanel.setBackingEnabled(false);
    m_connectionPanel.setPanelOpacity(0.94f);
    m_connectionPanel.setMaterialTextureEnabled(true);
    m_connectionPanel.setMaterialTextureIntensity(0.10f);

    m_storagePanel.setForceLiquidGlass(false);
    m_storagePanel.setLiquidGlassEnabled(false);
    m_storagePanel.setBlurEnabled(true);
    m_storagePanel.setBlurRadius(1.55f);
    m_storagePanel.setBlurPasses(1);
    m_storagePanel.setBackingEnabled(false);
    m_storagePanel.setPanelOpacity(0.90f);
    m_storagePanel.setMaterialTextureEnabled(true);
    m_storagePanel.setMaterialTextureIntensity(0.08f);
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
    // Keep the old pointer only for source compatibility. V6.3 never renders
    // it: the lockscreen owns an independently decoded texture instead.
    (void)texture;
    m_gameTexture = nullptr;
    const bool alreadyOwnsThisTitle =
        m_ownedGameTexture.valid() && m_ownedGameTextureTitleId == titleId;
    m_ownedGameTextureAttempted = alreadyOwnsThisTitle;
    m_deferredGameAssetReset = false;
    m_resumeHandoffPrepared = false;
    m_resumeBlackFrameRendered = false;
    m_resumeHandoffSent = false;
    m_unlockAudioStarted = false;
    m_gameTitle = title;
    m_gameTitleId = titleId;
    m_gameIsCard = gameCard;
    m_hasGame = titleId != 0;

    if (titleChanged)
        resetBackgroundAsset();
}

void LockScreenView::clearSuspendedGame() {
    // Do not free GPU-backed assets in the same update that hides the
    // lockscreen. The previous submitted frame may still reference them.
    // They are released at the beginning of the next clean reveal instead.
    m_gameTexture = nullptr;
    m_gameTitle.clear();
    m_gameTitleId = 0;
    m_gameIsCard = false;
    m_hasGame = false;
    m_deferredGameAssetReset = true;
    m_resumeHandoffPrepared = false;
    m_resumeBlackFrameRendered = false;
    m_resumeHandoffSent = false;
    m_unlockAudioStarted = false;
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
    const int previous = m_pressCount;
    m_pressCount = std::clamp(progress, 0, 3);
    m_pressFlash = clamp01(flash);
    m_progress.setProgress(m_pressCount);
    m_progress.setFlash(m_pressFlash);

    if (m_pressCount != previous)
        DebugLog::log("[lockscreen] press %d", m_pressCount);
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
    s_visiblyActive.store(
        m_unlocking || (m_viewOpacity > 0.05f && m_reveal > 0.02f));

    if (m_deferredGameAssetReset && !m_unlocking && m_reveal <= 0.001f) {
        if (auto* application = nxui::Application::current())
            application->gpu().waitIdle();
        m_ownedGameTexture = nxui::Texture{};
        m_ownedGameTextureTitleId = 0;
        m_ownedGameTextureAttempted = false;
        resetBackgroundAsset();
        m_deferredGameAssetReset = false;
    }

    const bool lockscreenVisible =
        !m_unlocking && m_viewOpacity > 0.05f && m_reveal > 0.02f;
    AudioManager::setLockscreenVisible(lockscreenVisible);
    AudioManager* audio = AudioManager::active();
    if (audio)
        audio->update();
    if (lockscreenVisible && !m_musicSceneEntered && audio) {
        m_musicSceneEntered = true;
        m_unlockAudioStarted = false;
        audio->playLockscreen(480);
    }

    if (m_unlocking && !m_unlockAudioStarted) {
        m_unlockAudioStarted = true;
        DebugLog::log("[lockscreen] unlock begin suspended=%d", m_hasGame ? 1 : 0);
        if (audio) {
            if (m_hasGame)
                audio->fadeOutForGame(420);
            else
                audio->playHome(420);
        }
    }

#ifdef SWITCHU_MENU
    // Safe direct handoff. The existing WiiUMenuApp code continues to own the
    // three-press animation, but the view takes over before its old final
    // clear/reload block. First render one completely black frame. On the next
    // update, wait for that frame, queue resume, then stop the app loop.
    if (m_unlocking && m_hasGame && !m_resumeHandoffSent) {
        if (m_unlockProgress >= 0.78f && !m_resumeHandoffPrepared) {
            m_resumeHandoffPrepared = true;
            DebugLog::log("[lockscreen] suspended handoff prepared; game texture disabled");
        }

        if (m_resumeHandoffPrepared && m_resumeBlackFrameRendered) {
            if (auto* application = nxui::Application::current()) {
                DebugLog::log("[lockscreen] black frame submitted; waiting for GPU");
                application->gpu().waitIdle();
                DebugLog::log("[lockscreen] gpu idle; queueing resume");

                if (audio)
                    audio->stop();

                const Result rc = switchu::menu::smi_cmd::resumeApplication();
                DebugLog::log("[lockscreen] resume command rc=0x%X", rc);
                if (R_SUCCEEDED(rc)) {
                    m_resumeHandoffSent = true;
                    DebugLog::log("[lockscreen] menu exit requested after resume queue");
                    application->requestExit();
                } else {
                    m_resumeHandoffPrepared = false;
                    m_resumeBlackFrameRendered = false;
                    if (audio)
                        audio->playLockscreen(220);
                }
            }
        }
    }
#endif

    if (!m_unlocking && m_viewOpacity <= 0.001f) {
        AudioManager::setLockscreenVisible(false);
        m_musicSceneEntered = false;
        m_unlockAudioStarted = false;
    }
}

void LockScreenView::applyThemeToWidgets() {
    const nxui::Color base = {0.030f, 0.040f, 0.100f, 0.18f};
    const nxui::Color border = {0.50f, 0.58f, 1.00f, 0.24f};
    const nxui::Color highlight = {0.92f, 0.98f, 1.00f, 0.10f};

    m_profilePanel.setBaseColor(base);
    m_profilePanel.setBorderColor(border);
    m_profilePanel.setHighlightColor(highlight);
    m_profilePanel.setBorderWidth(1.80f);
    m_profilePanel.setCornerRadius(25.f);

    m_connectionPanel.setBaseColor(base);
    m_connectionPanel.setBorderColor(border);
    m_connectionPanel.setHighlightColor(highlight);
    m_connectionPanel.setBorderWidth(1.20f);
    m_connectionPanel.setCornerRadius(28.f);

    m_storagePanel.setBaseColor(base);
    m_storagePanel.setBorderColor(border);
    m_storagePanel.setHighlightColor(highlight);
    m_storagePanel.setBorderWidth(1.15f);
    m_storagePanel.setCornerRadius(24.f);
}

void LockScreenView::resetBackgroundAsset() {
    // Repeated HOME notifications may rebuild the lockscreen while its last
    // frame is still in flight. Never destroy a texture until those commands
    // have completed.
    if (auto* application = nxui::Application::current())
        application->gpu().waitIdle();
    m_backgroundTexture = nxui::Texture{};
    m_backgroundPath.clear();
    m_backgroundAttempted = false;
}

void LockScreenView::resetProfileAsset() {
    if (auto* application = nxui::Application::current())
        application->gpu().waitIdle();
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

    if (m_hasGame && m_gameTitleId != 0 && !m_ownedGameTextureAttempted) {
        m_ownedGameTextureAttempted = true;
        const std::vector<std::uint8_t> iconData =
            AppListLoader::loadIconData(m_gameTitleId);
        if (!iconData.empty()) {
            nxui::Texture independentTexture;
            if (independentTexture.loadFromMemory(ren.gpu(), ren,
                                                   iconData.data(), iconData.size(), 320)) {
                // The upload is complete, then the old owned texture can be
                // replaced without racing the previous submitted frame.
                ren.gpu().waitIdle();
                m_ownedGameTexture = std::move(independentTexture);
                m_ownedGameTextureTitleId = m_gameTitleId;
                DebugLog::log("[lockscreen] independent game texture ready title=0x%016lX",
                              static_cast<unsigned long>(m_gameTitleId));
            } else {
                DebugLog::log("[lockscreen] independent game texture upload failed; using gradient fallback");
            }
        } else {
            DebugLog::log("[lockscreen] independent game icon unavailable; using gradient fallback");
        }
    }
}

void LockScreenView::updateStorageStatus() {
    s64 total = 0;
    s64 freeSpace = 0;
    const Result rc = nsGetStorageSize(NcmStorageId_SdCard,
                                       &total, &freeSpace);
    if (R_FAILED(rc) || total <= 0 || freeSpace < 0) {
        m_storageAvailable = false;
        m_sdTotalBytes = 0;
        m_sdFreeBytes = 0;
        return;
    }

    m_storageAvailable = true;
    m_sdTotalBytes = static_cast<std::uint64_t>(total);
    m_sdFreeBytes = static_cast<std::uint64_t>(freeSpace);
}

void LockScreenView::onUpdate(float dt) {
    m_animationTime += dt;
    m_progress.update(dt);

    // Softer, rapid press feedback. The target follows the existing press
    // pulse, while exponential smoothing removes the abrupt cut on both ends.
    const float pressShadeTarget = smoothStep(m_pressFlash);
    const float shadeSpeed = pressShadeTarget > m_pressShade ? 19.f : 13.f;
    const float shadeBlend = 1.f - std::exp(-shadeSpeed * std::max(0.f, dt));
    m_pressShade += (pressShadeTarget - m_pressShade) * shadeBlend;
    m_pressShade = clamp01(m_pressShade);

    m_storagePollTimer -= dt;
    if (m_storagePollTimer <= 0.f) {
        m_storagePollTimer = 5.f;
        updateStorageStatus();
    }

    m_connectionPollTimer -= dt;
    if (m_connectionPollTimer <= 0.f) {
        m_connectionPollTimer = 1.f;

        m_audioStatus = "Haut-parleurs de la console";
        if (bluetooth::IsAvailable()) {
            const BtmAudioDevice device = bluetooth::GetConnectedAudioDevice();
            bool knownPairedDevice = false;
            if (bluetooth::IsDeviceValid(device)) {
                const auto paired = bluetooth::ListPairedAudioDevices();
                for (const auto& candidate : paired) {
                    if (bluetooth::AddressesEqual(candidate.addr, device.addr)) {
                        knownPairedDevice = true;
                        break;
                    }
                }
            }

            // A few firmwares expose an internal/temporary audio endpoint
            // through btmsys. It is not a real Bluetooth headset and must not
            // replace the console-speaker status. Only a device that is both
            // connected and present in the paired-audio list is accepted.
            if (knownPairedDevice) {
                std::string name = cleanDeviceName(bluetooth::DeviceName(device));
                const std::string lowered = lowercaseAscii(name);
                const bool brokenShortName = splitUtf8(name).size() < 3;
                const bool internalEndpoint = lowered == "ch" || lowered == "audio";
                if (!brokenShortName && !internalEndpoint)
                    m_audioStatus = name;
                else
                    m_audioStatus = "Périphérique Bluetooth";
            }
        }

        const u32 handheldStyle = hidGetNpadStyleSet(HidNpadIdType_Handheld);
        if (handheldStyle != 0) {
            m_controllerStatus = "Joy-Con attachés";
        } else {
            static constexpr std::array<HidNpadIdType, 8> ids = {{
                HidNpadIdType_No1, HidNpadIdType_No2,
                HidNpadIdType_No3, HidNpadIdType_No4,
                HidNpadIdType_No5, HidNpadIdType_No6,
                HidNpadIdType_No7, HidNpadIdType_No8
            }};
            int connected = 0;
            for (const HidNpadIdType id : ids) {
                if (hidGetNpadStyleSet(id) != 0)
                    ++connected;
            }
            if (connected <= 0) {
                m_controllerStatus = "Aucune manette détectée";
            } else if (connected == 1) {
                m_controllerStatus = "Manette connectée";
            } else {
                m_controllerStatus = std::to_string(connected) +
                    " manettes connectées";
            }
        }
    }
}

void LockScreenView::onRender(nxui::Renderer& ren) {
    if (m_resumeHandoffPrepared && m_hasGame) {
        ren.drawRect({0.f, 0.f, 1280.f, 720.f},
                     nxui::Color(0.f, 0.f, 0.f, 1.f));
        m_resumeBlackFrameRendered = true;
        return;
    }

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

    // Global veil stays light enough to preserve the personalised background.
    ren.drawGradientRect(
        screen,
        nxui::Color(0.004f, 0.008f, 0.025f, 0.38f * m_viewOpacity),
        nxui::Color(0.004f, 0.006f, 0.020f, 0.72f * m_viewOpacity)
    );
    ren.drawRect(screen, nxui::Color(0.002f, 0.004f, 0.014f,
                                     0.17f * m_viewOpacity));

    // A local top veil stabilises the readability of the animated greeting,
    // clock, battery and profile without hiding the personalised background.
    ren.drawGradientRect(
        {0.f, 0.f, 1280.f, 232.f},
        nxui::Color(0.004f, 0.006f, 0.020f, 0.56f * m_viewOpacity),
        nxui::Color(0.020f, 0.008f, 0.052f, 0.025f * m_viewOpacity)
    );

    // Floating geometry is intentionally more visible when there is no
    // suspended game, where it becomes the main decorative background.
    drawFloatingShapes(ren, m_animationTime, contentAlpha, !m_hasGame);

    // Less intense than the old cut, with a quick fade-in/fade-out driven by
    // m_pressShade. It darkens the scene without swallowing the interface.
    if (m_pressShade > 0.001f) {
        ren.drawRect(screen,
                     nxui::Color(0.001f, 0.003f, 0.012f,
                                 0.105f * m_pressShade * contentAlpha));
    }

    // The full central composition moves upward, while the cover itself is
    // placed a few pixels below the exact ring centre as requested.
    const nxui::Vec2 center = {650.f, 330.f + lift * 0.10f};
    // Clock: fixed-position pieces prevent the minutes from moving when the
    // colon instantly disappears and reappears every second.
    ClockStrings clock;
    buildClockStrings(m_use12Hour, clock);
    if (m_fontLarge) {
        const float scale = 1.22f;
        const float x = 44.f;
        const float y = 24.f + lift * 0.04f;
        const nxui::Color white(0.985f, 0.992f, 1.f, 1.f);
        const float hourW = m_fontLarge->measure(clock.hour).x * scale;
        const float colonW = m_fontLarge->measure(":").x * scale;

        drawReadableText(ren, clock.hour, {x, y}, m_fontLarge,
                         white, scale, contentAlpha, 0.65f);
        if (clock.separatorVisible) {
            drawReadableText(ren, ":", {x + hourW + 1.f, y}, m_fontLarge,
                             white, scale, contentAlpha, 0.65f);
        }
        const float minuteX = x + hourW + colonW + 2.f;
        drawReadableText(ren, clock.minute, {minuteX, y}, m_fontLarge,
                         white, scale, contentAlpha, 0.65f);
        const float minuteW = m_fontLarge->measure(clock.minute).x * scale;

        if (!clock.suffix.empty() && m_fontMedium) {
            drawReadableText(ren, clock.suffix,
                             {minuteX + minuteW + 13.f, y + 39.f},
                             m_fontMedium,
                             nxui::Color(0.93f, 0.95f, 1.f, 0.94f),
                             0.68f, contentAlpha, 0.35f);
        }
    }
    if (m_fontMedium) {
        const float dateScale = 0.82f;
        drawReadableText(ren, clock.date,
                         {48.f, 124.f + lift * 0.04f},
                         m_fontMedium,
                         nxui::Color(0.95f, 0.97f, 1.f, 0.97f),
                         dateScale, contentAlpha, 0.45f);
    }
    // V6.5: the battery leaves the crowded date block and is right-aligned
    // in the upper-right area. The larger gap prevents percentage overlap.
    drawBattery(ren, m_fontNormal, {1092.f, 38.f + lift * 0.03f},
                m_batteryPercent, m_batteryCharging,
                0.72f + 0.28f * breathe, contentAlpha);

    // Larger adaptive greeting, now drawn without backing or shadow.
    float greetingScale = 1.30f;
    if (m_fontMedium) {
        const float maxWidth = 640.f;
        const float measured = m_fontMedium->measure(m_greeting).x;
        if (measured > 0.f)
            greetingScale = std::clamp(maxWidth / measured, 0.90f, 1.30f);
    }
    drawGradientText(ren, m_greeting, m_fontMedium,
                     {650.f, 55.f + lift * 0.03f},
                     greetingScale, contentAlpha, m_animationTime);

    if (m_hasGame) {
        // V6.3: the top of the ring is completely free. The whole central
        // composition is larger and the status is moved below it.
        // Final balance pass: the ring is more compact and slightly higher,
        // while the application cover gains visual importance.
        m_progress.setRect({410.f, 130.f + lift * 0.10f, 480.f, 400.f});
        m_progress.setOpacity(contentAlpha);
        m_progress.render(ren);

        const nxui::Rect cover = {539.f, 219.f + lift * 0.10f, 222.f, 222.f};
        // No coloured frame around the icon: only a quiet neutral backplate,
        // leaving the separate blue ring to carry the progress language.
        ren.drawRoundedRect(cover.expanded(5.f),
                            nxui::Color(0.002f, 0.005f, 0.020f,
                                        0.50f * contentAlpha), 34.f);
        ren.drawRoundedRect(cover,
                            nxui::Color(0.018f, 0.022f, 0.070f,
                                        0.96f * contentAlpha), 30.f);

        const nxui::Texture* gameTexture =
            (m_ownedGameTexture.valid() &&
             m_ownedGameTextureTitleId == m_gameTitleId)
                ? &m_ownedGameTexture
                : nullptr;
        const float gameTextureAlpha = contentAlpha *
            std::clamp(1.f - m_unlockProgress * 4.5f, 0.f, 1.f);
        if (gameTexture && gameTexture->valid() && gameTextureAlpha > 0.001f) {
            ren.drawTextureRounded(gameTexture, cover.shrunk(6.f), 25.f,
                                   nxui::Color::white().withAlpha(gameTextureAlpha));
        } else {
            ren.drawGradientRect(cover.shrunk(7.f),
                                 nxui::Color(0.16f, 0.28f, 0.62f, 0.76f * contentAlpha),
                                 nxui::Color(0.26f, 0.08f, 0.46f, 0.78f * contentAlpha));
        }
        ren.drawRoundedRectOutline(cover,
                                   nxui::Color(0.94f, 0.97f, 1.f,
                                               0.18f * contentAlpha),
                                   30.f, 1.2f);

        // The cover and ring already communicate that the application is
        // suspended. V6.5 removes the redundant status label and lets the
        // game title occupy the clean central gap below the cover.
        if (m_fontMedium && !m_gameTitle.empty()) {
            constexpr float titleScale = 0.84f;
            const nxui::Rect titleClip = {
                410.f, 526.f + lift * 0.14f, 480.f, 42.f
            };
            drawAutoScrollText(
                ren, m_gameTitle, titleClip, m_fontMedium,
                nxui::Color(0.985f, 0.992f, 1.f, 0.99f),
                titleScale, contentAlpha, m_animationTime, 0.f, true, 0.52f);
        }
    }

    // Larger compact identity card in the lower-left corner, away from the
    // clock and battery. It balances the right-side system widgets.
    const nxui::Rect profileRect = {38.f, 594.f + lift * 0.03f, 292.f, 92.f};
    m_profilePanel.setRect(profileRect);
    m_profilePanel.setOpacity(contentAlpha);
    m_profilePanel.render(ren);

    const nxui::Rect avatarRect = {
        profileRect.x + 16.f, profileRect.y + 14.f, 64.f, 64.f
    };
    if (m_profileAvatarTexture.valid()) {
        ren.drawTextureRounded(&m_profileAvatarTexture, avatarRect, 32.f,
                               nxui::Color::white().withAlpha(contentAlpha));
    } else {
        ren.drawCircle({avatarRect.x + 32.f, avatarRect.y + 32.f}, 32.f,
                       animatedLockGradient(0.16f,
                           std::fmod(m_animationTime / 3.6f, 1.f))
                           .withAlpha(0.88f * contentAlpha), 36);
        if (m_fontSmall && !m_profileName.empty()) {
            const auto glyphs = splitUtf8(m_profileName);
            const std::string initial = glyphs.empty() ? "?" : glyphs.front();
            const nxui::Vec2 size = m_fontSmall->measure(initial);
            drawReadableText(ren, initial,
                             {avatarRect.x + (avatarRect.width - size.x * 1.18f) * 0.5f,
                              avatarRect.y + 13.f},
                             m_fontSmall,
                             nxui::Color(1.f, 1.f, 1.f, 1.f),
                             1.18f, contentAlpha, 0.30f);
        }
    }
    ren.drawCircle({avatarRect.x + 32.f, avatarRect.y + 32.f}, 35.f,
                   nxui::Color(0.60f, 0.74f, 1.f,
                               0.075f * contentAlpha), 36);

    if (m_fontSmall) {
        drawReadableText(ren, "PROFIL ACTUEL",
                         {profileRect.x + 98.f, profileRect.y + 15.f},
                         m_fontSmall,
                         nxui::Color(0.46f, 0.78f, 1.f, 0.94f),
                         0.84f, contentAlpha, 0.24f);
    }
    if (m_fontMedium) {
        const nxui::Rect nameClip = {
            profileRect.x + 98.f, profileRect.y + 42.f,
            profileRect.width - 116.f, 34.f
        };
        drawAutoScrollText(
            ren, m_profileName, nameClip, m_fontMedium,
            nxui::Color(0.99f, 0.995f, 1.f, 0.98f),
            0.88f, contentAlpha, m_animationTime, 0.55f, false, 0.34f);
    }

    // One larger contextual card. Real blur is enabled on the GlassPanel;
    // the old opaque backing is removed so the personalised background remains visible.
    const nxui::Rect connectionRect = {930.f, 178.f + lift * 0.06f, 324.f, 244.f};
    m_connectionPanel.setRect(connectionRect);
    m_connectionPanel.setOpacity(contentAlpha);
    m_connectionPanel.render(ren);

    if (m_fontSmall) {
        drawReadableText(ren, "CONNEXIONS",
                         {connectionRect.x + 28.f, connectionRect.y + 22.f},
                         m_fontSmall,
                         nxui::Color(0.94f, 0.96f, 1.f, 0.97f),
                         1.12f, contentAlpha, 0.42f);
    }
    ren.drawRect({connectionRect.x + 28.f, connectionRect.y + 65.f,
                  connectionRect.width - 56.f, 1.f},
                 nxui::Color(0.78f, 0.84f, 1.f, 0.16f * contentAlpha));

    if (m_fontSmall && m_fontNormal) {
        drawReadableText(ren, "AUDIO",
                         {connectionRect.x + 28.f, connectionRect.y + 84.f},
                         m_fontSmall,
                         nxui::Color(0.46f, 0.78f, 1.f, 0.94f),
                         0.88f, contentAlpha, 0.25f);
        drawAutoScrollText(
            ren, m_audioStatus,
            {connectionRect.x + 28.f, connectionRect.y + 113.f,
             connectionRect.width - 56.f, 34.f},
            m_fontNormal,
            nxui::Color(0.99f, 0.995f, 1.f, 0.99f),
            0.86f, contentAlpha, m_animationTime, 0.85f, false, 0.30f);

        drawReadableText(ren, "MANETTE",
                         {connectionRect.x + 28.f, connectionRect.y + 157.f},
                         m_fontSmall,
                         nxui::Color(0.46f, 0.78f, 1.f, 0.94f),
                         0.88f, contentAlpha, 0.25f);
        drawAutoScrollText(
            ren, m_controllerStatus,
            {connectionRect.x + 28.f, connectionRect.y + 186.f,
             connectionRect.width - 56.f, 34.f},
            m_fontNormal,
            nxui::Color(0.99f, 0.995f, 1.f, 0.99f),
            0.86f, contentAlpha, m_animationTime, 1.25f, false, 0.30f);
    }


    // microSD storage card. The native NS service gives total and available
    // capacity without scanning the card contents.
    const nxui::Rect storageRect = {930.f, 442.f + lift * 0.06f, 324.f, 120.f};
    m_storagePanel.setRect(storageRect);
    m_storagePanel.setOpacity(contentAlpha);
    m_storagePanel.render(ren);

    if (m_fontSmall) {
        drawReadableText(ren, "CARTE MICROSD",
                         {storageRect.x + 24.f, storageRect.y + 16.f},
                         m_fontSmall,
                         nxui::Color(0.94f, 0.96f, 1.f, 0.97f),
                         0.94f, contentAlpha, 0.32f);
    }

    std::string storageText = "Carte microSD indisponible";
    float usedRatio = 0.f;
    if (m_storageAvailable && m_sdTotalBytes > 0) {
        const std::uint64_t safeFree = std::min(m_sdFreeBytes, m_sdTotalBytes);
        storageText = formatStorageAmount(safeFree) +
                      " libres sur " + formatStorageAmount(m_sdTotalBytes);
        usedRatio = clamp01(static_cast<float>(m_sdTotalBytes - safeFree) /
                            static_cast<float>(m_sdTotalBytes));
    }

    if (m_fontNormal) {
        drawAutoScrollText(
            ren, storageText,
            {storageRect.x + 24.f, storageRect.y + 49.f,
             storageRect.width - 48.f, 31.f},
            m_fontNormal,
            nxui::Color(0.99f, 0.995f, 1.f, 0.98f),
            0.78f, contentAlpha, m_animationTime, 1.65f, false, 0.26f);
    }

    const nxui::Rect storageTrack = {
        storageRect.x + 24.f, storageRect.y + 88.f,
        storageRect.width - 48.f, 8.f
    };
    ren.drawRoundedRect(storageTrack,
                        nxui::Color(0.16f, 0.20f, 0.34f,
                                    0.62f * contentAlpha), 4.f);
    if (usedRatio > 0.001f) {
        const nxui::Rect storageFill = {
            storageTrack.x, storageTrack.y,
            std::max(8.f, storageTrack.width * usedRatio), storageTrack.height
        };
        ren.drawRoundedRect(storageFill,
                            nxui::Color(0.08f, 0.58f, 1.00f,
                                        0.90f * contentAlpha), 4.f);
    }

    if (m_hasGame) {
        // With a suspended game, the large ring is the progress indicator.
        if (m_fontMedium && m_fontIcons) {
            const std::string instruction = "Appuie trois fois sur";
            const std::string aGlyph = utf8Codepoint(0xE0E0);
            const float textScale = 0.82f;
            const float pressKick = smoothStep(m_pressFlash);
            const float glyphScale = 1.24f * (1.f - 0.10f * pressKick);
            const nxui::Vec2 textSize = m_fontMedium->measure(instruction);
            const nxui::Vec2 glyphSize = m_fontIcons->measure(aGlyph);
            const float gap = 15.f;
            const float totalW = textSize.x * textScale + gap +
                                 glyphSize.x * 1.24f;
            const float startX = 650.f - totalW * 0.5f;
            const float y = 606.f + lift;

            drawReadableText(ren, instruction, {startX, y}, m_fontMedium,
                             nxui::Color(0.98f, 0.99f, 1.f, 0.98f),
                             textScale, contentAlpha, 0.38f);
            const float glyphX = startX + textSize.x * textScale + gap;
            const nxui::Vec2 glyphCenter = {
                glyphX + glyphSize.x * 1.24f * 0.5f,
                y + glyphSize.y * 1.24f * 0.47f
            };
            // Small white glow: immediate feedback without adding another
            // multicolour focal point below the game title.
            ren.drawCircle(glyphCenter,
                           21.f + 5.f * pressKick + 1.2f * breathe,
                           nxui::Color(1.f, 1.f, 1.f,
                               (0.045f + 0.11f * pressKick +
                                0.018f * breathe) * contentAlpha), 32);
            ren.drawCircle(glyphCenter,
                           16.f + 2.f * pressKick,
                           nxui::Color(0.92f, 0.97f, 1.f,
                               (0.025f + 0.055f * pressKick) * contentAlpha), 28);
            const float adjustedGlyphX =
                glyphCenter.x - glyphSize.x * glyphScale * 0.5f;
            const float adjustedGlyphY =
                glyphCenter.y - glyphSize.y * glyphScale * 0.47f;
            ren.drawText(aGlyph, {adjustedGlyphX, adjustedGlyphY - 3.f},
                         m_fontIcons,
                         nxui::Color(0.995f, 1.f, 1.f, contentAlpha),
                         glyphScale);
        }
    } else {
        // No suspended application: restore the V5-style breathing capsule
        // and three small press dots.
        drawNoGameUnlockPill(ren, m_fontMedium, m_fontIcons,
                             m_pressCount, m_pressFlash, m_pulse,
                             contentAlpha, lift);
    }

    if (m_unlocking) {
        const float flash = std::sin(std::min(1.f, m_unlockProgress * 1.28f) * kPi);
        const float eased = easeOutCubic(m_unlockProgress);
        const float radius = 34.f + 500.f * eased;
        drawArc(ren, center, radius, 0.f, 2.f * kPi,
                nxui::Color(0.70f, 0.90f, 1.f,
                            0.30f * flash * m_viewOpacity),
                2.4f, 128);

        // The suspended-game transition deliberately avoids the offscreen
        // wave effect. On real hardware it could leave pixel artefacts while
        // the application was taking the foreground back.
        if (!m_hasGame &&
            m_unlockProgress > 0.10f && m_unlockProgress < 0.88f) {
            ren.captureToOffscreen();
            ren.applyWave(m_pulse,
                          0.005f + 0.010f * flash,
                          22.f);
        }

        const float veil = 0.08f * smoothStep(flash);
        ren.drawRect(screen, nxui::Color(0.88f, 0.95f, 1.f,
                                         veil * m_viewOpacity));
    }
}
