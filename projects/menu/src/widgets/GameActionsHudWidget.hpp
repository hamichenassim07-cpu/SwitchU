#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Font.hpp>

class GameActionsHudWidget : public nxui::Widget {
public:
    void setFont(nxui::Font* font) { m_font = font; }
    void setIconFont(nxui::Font* font) { m_iconFont = font; }

protected:
    void onRender(nxui::Renderer& ren) override;

private:
    nxui::Font* m_font = nullptr;
    nxui::Font* m_iconFont = nullptr;
};
