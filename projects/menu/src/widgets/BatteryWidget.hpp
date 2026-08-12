#pragma once
#include <nxui/widgets/GlassWidget.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Types.hpp>
#include <cstdint>

class BatteryWidget : public nxui::GlassWidget {
public:
    BatteryWidget();
    ~BatteryWidget() override;
    void setFont(nxui::Font* f) { m_font = f; }
    void setTextColor(const nxui::Color& c) { m_textColor = c; }
    void setBatteryStatus(uint32_t percentage, bool charging);

protected:
    void onRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;
    void onContentRender(nxui::Renderer& ren) override;
    nxui::Vec2 computeContentSize() const override;

private:
    nxui::Font* m_font = nullptr;
    float m_level = -1.f;
    bool m_charging = false;
    float m_timer = 0.f;
    float m_chargeAnim = 0.f;
    float m_wifiTimer = 1.f;
    bool m_nifmReady = false;
    bool m_wifiRadioEnabled = false;
    bool m_wifiConnected = false;
    uint32_t m_wifiStrength = 0;
    nxui::Color m_textColor {1.f, 1.f, 1.f, 1.f};
};
