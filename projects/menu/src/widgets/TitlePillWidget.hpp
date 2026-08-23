#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Types.hpp>
#include <string>
#include <cstdint>

class TitlePillWidget : public nxui::GlassWidget {
public:
    TitlePillWidget();

    void setFont(nxui::Font* f)              { m_font = f; }
    void setIconFont(nxui::Font* f)          { m_iconFont = f; }
    void setTextColor(const nxui::Color& c)  { m_textColor = c; }

    void setAnchor(float centerX, float topY, float screenWidth = 1280.f);
    void setText(const std::string& text, float screenWidth = 1280.f);
    void setSelectedTitleId(std::uint64_t titleId);
    void setGameActionsVisible(bool visible);
    bool gameActionsVisible() const { return m_showGameActions; }
    void setMoveMode(bool active) { m_moveMode = active; }
    bool moveMode() const { return m_moveMode; }

    // V10.6 profile mode. The implementation below reuses the exact
    // TitlePillWidget behaviour from Switch U master for profile focus.
    void setProfileOriginalMode(bool enabled);
    bool profileOriginalMode() const { return m_profileOriginalMode; }
    void hideAnimated(float screenWidth = 1280.f);

protected:
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    float anchoredX(float width, float screenWidth) const;

    nxui::Font*       m_font = nullptr;
    nxui::Font*       m_iconFont = nullptr;
    std::string       m_text;
    nxui::Color       m_textColor {1.f, 1.f, 1.f, 1.f};
    nxui::AnimatedFloat m_animX{0.f};
    nxui::AnimatedFloat m_animW{0.f};
    nxui::AnimatedFloat m_textReveal{1.f};
    float m_anchorCenterX = 640.f;
    bool m_layoutInitialized = false;
    bool m_hideOnCollapse = false;
    bool m_showGameActions = false;
    bool m_moveMode = false;
    bool m_profileOriginalMode = false;
    std::uint64_t m_selectedTitleId = 0;
    std::uint64_t m_playTimeMinutes = 0;
    bool m_playTimeAvailable = false;
};
