#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/Theme.hpp>

#include <array>
#include <algorithm>

// V7.1: the validated glow can now follow a perspective-projected 3D outline.
class LockPressIndicator : public nxui::Widget {
public:
    static constexpr int MaxOutlinePoints = 32;

    void setTheme(const nxui::Theme* theme) { m_theme = theme; }
    void setProgress(int progress) {
        m_progress = progress < 0 ? 0 : (progress > 3 ? 3 : progress);
    }
    void setVisualProgress(float progress) { m_visualProgress = progress; }
    void setPulse(float pulse) { m_pulse = pulse; }
    void setFlash(float flash) { m_flash = flash; }

    void setProjectedOutline(const nxui::Vec2* points, int count) {
        m_outlineCount = std::clamp(count, 0, MaxOutlinePoints);
        for (int i = 0; i < m_outlineCount; ++i)
            m_outline[i] = points[i];
    }
    void clearProjectedOutline() { m_outlineCount = 0; }

protected:
    void onRender(nxui::Renderer& ren) override;

private:
    const nxui::Theme* m_theme = nullptr;
    int m_progress = 0;
    float m_visualProgress = 0.f;
    float m_pulse = 0.f;
    float m_flash = 0.f;
    std::array<nxui::Vec2, MaxOutlinePoints> m_outline {};
    int m_outlineCount = 0;
};
