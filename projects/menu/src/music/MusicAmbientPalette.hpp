#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace switchu::menu::music::ambient {

struct Rgb {
    float r = 0.f, g = 0.f, b = 0.f;
};

// Background-only colours. These never replace the sleeve/selection accent.
struct Palette {
    std::array<Rgb, 3> colours{{{0.50f, 0.50f, 0.52f},
                               {0.40f, 0.42f, 0.45f},
                               {0.48f, 0.45f, 0.44f}}};
    bool sampled = false;
};

// Bounded sampling of an ALREADY decoded image: <= 24 x 24 pixels, no heap,
// file I/O, decoder, worker, or dependence on carousel state.
Palette samplePalette(const uint8_t* rgba, size_t bytes, int width, int height);

inline constexpr size_t kRibbonSegments = 48;
inline constexpr size_t kRibbonLayers = 3; // one main ribbon, two veils
inline constexpr float kPaletteTransitionSeconds = 0.85f;

struct RibbonColumn {
    float x = 0.f, centre = 0.f, halfWidth = 0.f;
    float upperLight = 0.f, lowerLight = 0.f, tintMix = 0.f;
};

class Motion {
public:
    void setTarget(const Palette& palette);
    void update(float dt);
    void ribbon(size_t layer, std::array<RibbonColumn, kRibbonSegments + 1>& out) const;
    const Palette& palette() const { return m_current; }
    double phase() const { return m_phase; }

private:
    Palette m_current{};
    Palette m_from{};
    Palette m_target{};
    float m_transition = kPaletteTransitionSeconds;
    double m_phase = 0.0;
};

} // namespace switchu::menu::music::ambient
