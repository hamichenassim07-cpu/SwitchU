#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/Theme.hpp>

// V6: the three presses are displayed as three large luminous segments
// around the suspended application's cover.
class LockPressIndicator : public nxui::Widget {
public:
    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setProgress(int progress) {
        m_progress = progress < 0 ? 0 : (progress > 3 ? 3 : progress);
    }
    void setPulse(float pulse) { m_pulse = pulse; }
    void setFlash(float flash) { m_flash = flash; }

protected:
    void onRender(nxui::Renderer& ren) override;

private:
    const nxui::Theme* m_theme = nullptr;
    int m_progress = 0;
    float m_pulse = 0.f;
    float m_flash = 0.f;
};
