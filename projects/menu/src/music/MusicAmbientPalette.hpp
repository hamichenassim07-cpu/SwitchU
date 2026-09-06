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
    std::array<Rgb, 3> colours{{{0.34f, 0.43f, 0.56f},
                               {0.26f, 0.34f, 0.45f},
                               {0.39f, 0.40f, 0.46f}}};
    bool sampled = false;
};

// Bounded sampling of an ALREADY decoded image: <= 24 x 24 pixels, no heap,
// file I/O, decoder, worker, or dependence on carousel state.
Palette samplePalette(const uint8_t* rgba, size_t bytes, int width, int height);

struct Light {
    float x = 0.f, y = 0.f, width = 0.f, height = 0.f, opacity = 0.f;
    Rgb colour{};
};

class Motion {
public:
    void setTarget(const Palette& palette) { m_target = palette; }
    void update(float dt);
    std::array<Light, 4> lights() const;
    const Palette& palette() const { return m_current; }
    double phase() const { return m_phase; }

private:
    Palette m_current{};
    Palette m_target{};
    double m_phase = 0.0;
};

} // namespace switchu::menu::music::ambient
