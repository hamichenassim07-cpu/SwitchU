#include "MusicAmbientPalette.hpp"

#include <algorithm>
#include <cmath>

namespace switchu::menu::music::ambient {
namespace {
float distanceSquared(Rgb a, Rgb b) {
    const float r = a.r - b.r, g = a.g - b.g, blue = a.b - b.b;
    return r*r + g*g + blue*blue;
}

Rgb lightColour(Rgb c) {
    // Lift genuinely dark artwork without inventing a hue for monochrome art.
    const float peak = std::max({c.r, c.g, c.b});
    if (peak < 0.025f) return {0.32f, 0.32f, 0.32f};
    const float gain = std::clamp(peak, 0.48f, 0.82f) / peak;
    return {c.r * gain, c.g * gain, c.b * gain};
}
} // namespace

Palette samplePalette(const uint8_t* rgba, size_t bytes, int width, int height) {
    Palette out{};
    if (!rgba || width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return out;
    const uint64_t required = uint64_t(width) * uint64_t(height) * 4u;
    if (required > bytes) return out;

    struct Bin { float weight = 0.f, r = 0.f, g = 0.f, b = 0.f; };
    // 12 hue families, two brightness bands, plus three neutral bands.
    std::array<Bin, 27> bins{};
    const int columns = std::min(width, 24), rows = std::min(height, 24);
    for (int gy = 0; gy < rows; ++gy) {
        const int y = (2 * gy + 1) * height / (2 * rows);
        for (int gx = 0; gx < columns; ++gx) {
            const int x = (2 * gx + 1) * width / (2 * columns);
            const size_t i = (size_t(y) * size_t(width) + size_t(x)) * 4u;
            const float alpha = rgba[i + 3] / 255.f;
            if (alpha < 0.35f) continue;
            const float r = rgba[i] / 255.f, g = rgba[i + 1] / 255.f,
                        b = rgba[i + 2] / 255.f;
            const float hi = std::max({r,g,b}), lo = std::min({r,g,b});
            const float delta = hi - lo;
            const float saturation = hi > 0.0001f ? delta / hi : 0.f;
            int bin = 24 + std::min(2, int(hi * 3.f));
            if (saturation >= 0.14f && hi >= 0.06f) {
                float hue = hi == r ? (g-b)/delta :
                            (hi == g ? 2.f+(b-r)/delta : 4.f+(r-g)/delta);
                if (hue < 0.f) hue += 6.f;
                bin = std::min(11, int(hue * 2.f)) * 2 + (hi >= 0.5f ? 1 : 0);
            }
            const float weight = alpha * (0.65f + 0.35f * saturation);
            Bin& bucket = bins[size_t(bin)];
            bucket.weight += weight;
            bucket.r += r * weight;
            bucket.g += g * weight;
            bucket.b += b * weight;
        }
    }

    std::array<Rgb, 3> chosen{};
    size_t count = 0;
    for (size_t pass = 0; pass < bins.size() && count < chosen.size(); ++pass) {
        auto best = std::max_element(bins.begin(), bins.end(),
            [](const Bin& a, const Bin& b) { return a.weight < b.weight; });
        if (best->weight <= 0.f) break;
        const Rgb colour{best->r / best->weight, best->g / best->weight,
                         best->b / best->weight};
        best->weight = 0.f;
        bool distinct = true;
        for (size_t j = 0; j < count; ++j)
            if (distanceSquared(colour, chosen[j]) < 0.035f) distinct = false;
        if (distinct) chosen[count++] = colour;
    }
    if (count == 0) return out;
    for (size_t i = 0; i < out.colours.size(); ++i)
        out.colours[i] = lightColour(chosen[i < count ? i : 0]);
    out.sampled = true;
    return out;
}

void Motion::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0.f) return;
    // Waking from sleep must not jump the lighting or restart its phase.
    const float step = std::min(dt, 0.05f);
    m_phase += double(step);
    const float blend = -std::expm1(-step / 0.48f);
    for (size_t i = 0; i < m_current.colours.size(); ++i) {
        auto& a = m_current.colours[i];
        const auto& b = m_target.colours[i];
        a.r += (b.r - a.r) * blend;
        a.g += (b.g - a.g) * blend;
        a.b += (b.b - a.b) * blend;
    }
    m_current.sampled = m_target.sampled;
}

std::array<Light, 4> Motion::lights() const {
    // Four broad, overlapping fields. Motion is independent of input, album
    // changes and playback; only their colours approach a new target palette.
    const float a = float(std::sin(m_phase * 0.19));
    const float b = float(std::cos(m_phase * 0.13));
    const float c = float(std::sin(m_phase * 0.16 + 1.7));
    const float d = float(std::cos(m_phase * 0.11 + 0.8));
    return {{{220.f + 120.f*a, 220.f + 65.f*b, 1080.f, 660.f, 0.70f, m_current.colours[0]},
             {980.f + 125.f*c, 180.f + 70.f*d, 1050.f, 720.f, 0.61f, m_current.colours[1]},
             {630.f + 170.f*b, 420.f + 45.f*a,  930.f, 580.f, 0.53f, m_current.colours[2]},
             {620.f + 210.f*d, 100.f + 50.f*c, 1240.f, 480.f, 0.27f, m_current.colours[0]}}};
}
} // namespace switchu::menu::music::ambient
