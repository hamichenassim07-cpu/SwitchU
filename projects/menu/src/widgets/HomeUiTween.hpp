#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace switchu::homeui {
// Bit test remains valid with devkitPro's -ffast-math. Never catch up a UI
// animation by several seconds after sleep or an interrupted frame.
inline float uiDelta(float dt) {
    uint32_t bits = 0;
    std::memcpy(&bits, &dt, sizeof(bits));
    return (bits & 0x7f800000u) == 0x7f800000u ? 0.f : std::clamp(dt, 0.f, .05f);
}
class UiTween {
public:
    explicit UiTween(float initial = 0.f) : m_value(initial), m_from(initial), m_target(initial) {}
    float value() const { return m_value; }
    void target(float next, float seconds) {
        if (next == m_target) return;
        m_from = m_value;
        m_target = next;
        m_elapsed = 0.f;
        m_duration = std::max(.001f, seconds);
    }
    bool update(float dt) {
        if (m_value == m_target) return false;
        m_elapsed = std::min(m_duration, m_elapsed + uiDelta(dt));
        const float t = m_elapsed / m_duration;
        const float ease = t * t * (3.f - 2.f * t);
        m_value = m_elapsed == m_duration ? m_target : m_from + (m_target - m_from) * ease;
        return true;
    }
private:
    float m_value, m_from, m_target;
    float m_elapsed = 0.f, m_duration = .2f;
};
}
