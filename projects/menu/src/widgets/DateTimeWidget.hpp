#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>
#include <string>

class DateTimeWidget : public nxui::GlassWidget {
public:
    DateTimeWidget();
    void setFont(nxui::Font* f) { m_font = f; }
    void setSmallFont(nxui::Font* sf) { m_smallFont = sf; }
    void setIconFont(nxui::Font* f) { m_iconFont = f; }
    void setTextColor(const nxui::Color& c) { m_textColor = c; }
    void setSecondaryTextColor(const nxui::Color& c) { m_secondaryColor = c; }
    void setUse12HourClock(bool enabled);

    // V10 HOME category selector. The widget keeps rendering the clock in its
    // historical top-left box while also owning the centered Jeux/Applications
    // capsule used as a focus target by WiiUMenuAppInteraction.cpp.
    void setHomeApplicationsActive(bool active);
    void setHomeCategory(int category); // 0 Jeux, 1 Applications, 2 Musique
    int homeCategory() const { return m_homeCategory; }
    bool homeApplicationsActive() const { return m_homeCategory == 1; }
    void setHomeTabsVisible(bool visible) { m_homeTabsVisible = visible; }
    bool homeTabsVisible() const { return m_homeTabsVisible; }
    void setHomeTabsFocused(bool focused) { m_homeTabsFocused = focused; }
    nxui::Rect activeHomeTabRect() const;
    nxui::Rect homeTabsRect() const;

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    nxui::Font* m_iconFont = nullptr;
    float m_timer = 0.f;
    std::string m_timeStr;
    std::string m_dateStr;
    bool m_use12HourClock = false;
    nxui::Color m_textColor      {1.f, 1.f, 1.f, 1.f};
    nxui::Color m_secondaryColor {0.7f, 0.7f, 0.8f, 0.8f};

    int m_homeCategory = 0;
    bool m_homeTabsFocused = false;
    bool m_homeTabsVisible = true;
    float m_homeTabSlide = 0.f;
    float m_homeTabPop = 0.f;
    float m_homeTabAnimFrom = 0.f;
    float m_homeTabAnimTo = 0.f;
    float m_homeTabAnimTime = 0.f;
    bool m_homeTabAnimating = false;
};
