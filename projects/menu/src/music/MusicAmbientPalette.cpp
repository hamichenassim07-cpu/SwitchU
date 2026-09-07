#include "MusicAmbientPalette.hpp"

#include <algorithm>
#include <bit>
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

void Motion::setTarget(const Palette& palette) {
    bool same = true;
    for (size_t i = 0; i < palette.colours.size(); ++i) {
        const auto& a = palette.colours[i];
        const auto& b = m_target.colours[i];
        same &= a.r == b.r && a.g == b.g && a.b == b.b;
    }
    // The cache can offer the same palette on every update. Only an actual
    // colour change starts a tween, from precisely the currently shown colour.
    if (same) return;
    m_from = m_current;
    m_target = palette;
    m_transition = 0.f;
}

void Motion::update(float dt) {
    // A bit check also remains valid with the project's -ffast-math build.
    if ((std::bit_cast<uint32_t>(dt) & 0x7f800000u) == 0x7f800000u || dt <= 0.f)
        return;
    const float step = std::min(dt, 0.05f);
    m_phase += double(step);
    m_transition = std::min(kPaletteTransitionSeconds, m_transition + step);
    const float t = m_transition / kPaletteTransitionSeconds;
    const float blend = t*t*(3.f - 2.f*t);
    for (size_t i = 0; i < m_current.colours.size(); ++i) {
        const auto& a = m_from.colours[i];
        const auto& b = m_target.colours[i];
        m_current.colours[i] = {a.r + (b.r-a.r)*blend,
                               a.g + (b.g-a.g)*blend,
                               a.b + (b.b-a.b)*blend};
    }
    m_current.sampled = m_target.sampled;
}

void Motion::ribbon(size_t layer,
                    std::array<RibbonColumn, kRibbonSegments + 1>& out) const {
    // Six moving control sections, joined by cubic Hermite curves. Unequal
    // heights and evolving slopes make long folds, rather than a scrolling
    // texture or a uniform sine wave. Every cross-section stays strictly open.
    layer = std::min(layer, kRibbonLayers - 1);
    constexpr std::array<float, 6> silhouette{8.f, 38.f, 11.f, -37.f, -25.f, 14.f};
    constexpr std::array<float, 6> widths{41.f, 57.f, 33.f, 53.f, 46.f, 31.f};
    constexpr std::array<float, 3> speed{0.113f, 0.157f, 0.131f};
    constexpr std::array<float, 3> offset{-27.f, 0.f, 18.f};
    constexpr std::array<float, 3> breadth{0.94f, 1.12f, 0.65f};
    std::array<float, 6> heights{}, halfWidths{}, upper{}, lower{}, tint{};
    for (size_t i = 0; i < heights.size(); ++i) {
        const double phase = m_phase * double(speed[layer]) + i*1.17 + layer*1.8;
        heights[i] = 345.f + offset[layer] + silhouette[i] +
                     22.f*float(std::sin(phase)) +
                     10.f*float(std::cos(m_phase*0.071 + i*0.71 + layer));
        halfWidths[i] = breadth[layer] * widths[i] *
                       (1.f + 0.23f*float(std::cos(phase*0.83 + 1.4)));
        // Highlights travel in patches, never as a permanent white outline.
        upper[i] = std::max(0.f, float(std::sin(phase*0.69 + i*0.37)));
        lower[i] = std::max(0.f, float(std::cos(phase*0.77 + 1.2)));
        tint[i] = 0.5f + 0.35f*float(std::sin(phase*0.53));
    }
    const auto curve = [](const std::array<float, 6>& values, size_t k, float t) {
        const float a = values[k], b = values[k+1];
        const float ma = k == 0 ? b-a : (b-values[k-1])*0.5f;
        const float mb = k == 4 ? b-a : (values[k+2]-a)*0.5f;
        const float t2=t*t, t3=t2*t;
        return (2*t3-3*t2+1)*a + (t3-2*t2+t)*ma +
               (-2*t3+3*t2)*b + (t3-t2)*mb;
    };
    for (size_t i = 0; i < out.size(); ++i) {
        const float u = float(i) / kRibbonSegments;
        const float position = u*5.f;
        const size_t k = std::min(size_t(position), size_t(4));
        const float t = position - float(k);
        out[i] = {-64.f + 1408.f*u, curve(heights,k,t),
                  std::clamp(curve(halfWidths,k,t), 14.f, 78.f),
                  std::clamp(curve(upper,k,t),0.f,1.f),
                  std::clamp(curve(lower,k,t),0.f,1.f),
                  std::clamp(curve(tint,k,t),0.f,1.f)};
    }
}
} // namespace switchu::menu::music::ambient
