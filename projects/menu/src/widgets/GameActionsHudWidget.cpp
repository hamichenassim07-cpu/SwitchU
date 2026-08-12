#include "GameActionsHudWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <string>

namespace {
constexpr float kScreenCenterX = 640.f;
constexpr float kSeparatorY = 596.f;
constexpr float kSeparatorWidth = 364.f;
constexpr float kSeparatorHeight = 2.4f;
constexpr float kActionsY = 622.f;
constexpr float kActionLabelScale = 0.83f;
constexpr float kActionGlyphScale = 0.97f;
constexpr float kActionGap = 8.f;
constexpr float kPairGap = 42.f;

std::string utf8Codepoint(unsigned cp) {
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
} // namespace

void GameActionsHudWidget::onRender(nxui::Renderer& ren) {
    if (!m_font || m_opacity <= 0.001f)
        return;

    // V10.9: this separator and action strip live at absolute screen
    // coordinates. Their geometry is intentionally independent from the
    // selected game's title widget and therefore cannot move when title width
    // changes.
    ren.drawRoundedRect(
        {kScreenCenterX - kSeparatorWidth * 0.5f,
         kSeparatorY,
         kSeparatorWidth,
         kSeparatorHeight},
        nxui::Color(0.94f, 0.97f, 1.00f, 0.30f * m_opacity),
        1.1f
    );

    auto& i18n = nxui::I18n::instance();
    const std::string launch = i18n.tr("hint.launch", "Lancer");
    const std::string move = i18n.tr("hint.move", "Déplacer");
    const std::string aGlyph = utf8Codepoint(0xE0E0);
    const std::string yGlyph = utf8Codepoint(0xE0E3);
    nxui::Font* glyphFont = m_iconFont ? m_iconFont : m_font;

    const nxui::Vec2 launchBase = m_font->measure(launch);
    const nxui::Vec2 moveBase = m_font->measure(move);
    const nxui::Vec2 aBase = glyphFont->measure(aGlyph);
    const nxui::Vec2 yBase = glyphFont->measure(yGlyph);

    const float launchW = launchBase.x * kActionLabelScale;
    const float moveW = moveBase.x * kActionLabelScale;
    const float aW = aBase.x * kActionGlyphScale;
    const float yW = yBase.x * kActionGlyphScale;
    const float firstW = aW + kActionGap + launchW;
    const float secondW = yW + kActionGap + moveW;
    const float rowW = firstW + kPairGap + secondW;
    float x = kScreenCenterX - rowW * 0.5f;

    auto drawAction = [&](const std::string& glyph,
                          const std::string& label,
                          float glyphW,
                          float labelW) {
        const nxui::Vec2 glyphBase = glyphFont->measure(glyph);
        const nxui::Vec2 labelBase = m_font->measure(label);
        const float glyphH = glyphBase.y * kActionGlyphScale;
        const float labelH = labelBase.y * kActionLabelScale;
        const float rowH = std::max(glyphH, labelH);

        ren.drawText(
            glyph,
            {x, kActionsY + (rowH - glyphH) * 0.5f},
            glyphFont,
            nxui::Color(0.98f, 1.f, 0.99f, 0.98f * m_opacity),
            kActionGlyphScale
        );
        ren.drawText(
            label,
            {x + glyphW + kActionGap,
             kActionsY + (rowH - labelH) * 0.5f},
            m_font,
            nxui::Color(0.94f, 0.96f, 0.98f, 0.92f * m_opacity),
            kActionLabelScale
        );
        x += glyphW + kActionGap + labelW;
    };

    drawAction(aGlyph, launch, aW, launchW);
    x += kPairGap;
    drawAction(yGlyph, move, yW, moveW);
}
