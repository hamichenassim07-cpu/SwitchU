#include "MusicAmbientBackground.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace switchu::menu::music {
namespace {
float smooth(float a, float b, float x) {
    const float t = std::clamp((x-a)/(b-a), 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}
ambient::Rgb mix(ambient::Rgb a, ambient::Rgb b, float t) {
    return {a.r+(b.r-a.r)*t, a.g+(b.g-a.g)*t, a.b+(b.b-a.b)*t};
}
struct Vertex { nxui::Vec2 p; nxui::Color c; };
void quad(nxui::Renderer& ren, const Vertex& a, const Vertex& b,
          const Vertex& c, const Vertex& d) {
    ren.drawColoredTriangle(a.p,a.c,b.p,b.c,c.p,c.c);
    ren.drawColoredTriangle(a.p,a.c,c.p,c.c,d.p,d.c);
}
float visibility(float y, float floorY) {
    // Completely transparent twelve pixels BEFORE the real horizon. The
    // independent scissor below is a second barrier, including on transitions.
    return smooth(142.f,218.f,y) * (1.f-smooth(floorY-84.f,floorY-12.f,y));
}
}

void MusicAmbientBackground::drawBase(nxui::Renderer& ren, float alpha) {
    ren.drawGradientRect({0.f, 0.f, 1280.f, 720.f},
                        {0.026f, 0.029f, 0.037f, alpha},
                        {0.009f, 0.011f, 0.016f, alpha});
}

void MusicAmbientBackground::draw(nxui::Renderer& ren, float alpha, float floorY) {
    if (alpha <= 0.f || floorY <= 142.f) return;
    const auto& palette = m_motion.palette().colours;
    ren.pushClipRect({0.f, 0.f, 1280.f, floorY-12.f});

    // Broad continuous light field, with a deliberately coarse 8 x 7 mesh.
    // It only contains palette colours: no artwork image, radial sprites,
    // blur pass, shader switch, framebuffer or image upload is involved.
    constexpr std::array<float,8> rows{0.f,110.f,218.f,315.f,385.f,430.f,460.f,482.f};
    std::array<Vertex,9> previous{}, current{};
    const float drift = float(std::sin(m_motion.phase()*0.083))*0.12f;
    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; x < current.size(); ++x) {
            const float u = float(x)/8.f;
            const float v = rows[y]/494.f;
            const auto colour = mix(mix(palette[0],palette[1],
                                        smooth(0.f,1.f,u+drift+(v-0.5f)*0.32f)),
                                    palette[2], 0.25f*smooth(0.f,1.f,v+u*0.3f));
            const float strength = (0.016f + 0.19f*smooth(90.f,350.f,rows[y])) *
                                   (1.f-smooth(floorY-108.f,floorY-12.f,rows[y]));
            current[x] = {{1280.f*u,rows[y]},
                           {colour.r,colour.g,colour.b,strength*alpha}};
        }
        if (y != 0)
            for (size_t x = 0; x+1 < current.size(); ++x)
                quad(ren,previous[x],previous[x+1],current[x+1],current[x]);
        previous = current;
    }

    // A sparse satin cross-section: narrow, intermittently lit crests, a
    // diffuse body and translucent centre. Colour interpolation happens on
    // the GPU using the HOME renderer's existing Basic vertex format.
    constexpr std::array<float,11> section{0.f,.018f,.05f,.14f,.34f,.59f,.81f,.93f,.975f,.99f,1.f};
    constexpr std::array<float,11> opacity{0.f,.20f,.34f,.27f,.09f,.075f,.17f,.35f,.18f,.07f,0.f};
    constexpr std::array<float,3> layerOpacity{.42f,.92f,.36f};
    std::array<Vertex,section.size()> left{}, right{};
    for (size_t layer = 0; layer < ambient::kRibbonLayers; ++layer) {
        m_motion.ribbon(layer,m_columns);
        for (size_t x = 0; x < m_columns.size(); ++x) {
            const auto& column = m_columns[x];
            const size_t colourIndex = layer == 1 ? 0 : (layer == 0 ? 1 : 2);
            const auto body = mix(palette[colourIndex],palette[(colourIndex+1)%3],column.tintMix*0.32f);
            const float peak = std::max({body.r,body.g,body.b});
            for (size_t j = 0; j < section.size(); ++j) {
                const float y = column.centre - 18.f + (section[j]*2.f-1.f)*column.halfWidth;
                const float upper = j == 1 || j == 2 ? column.upperLight : 0.f;
                const float lower = j == 8 || j == 9 ? column.lowerLight : 0.f;
                const float sheen = std::max(upper, lower*0.72f);
                const float gain = 0.97f + sheen*0.37f;
                // Highlights lighten the album hue; no hard-coded cyan and
                // no white additive overexposure on pale or monochrome art.
                const auto channel = [&](float c) {
                    return std::min(0.90f,c*gain + peak*0.16f*sheen);
                };
                right[j] = {{column.x,y},
                    {channel(body.r),channel(body.g),channel(body.b),
                     (opacity[j]+0.62f*sheen)*layerOpacity[layer]*visibility(y,floorY)*alpha}};
            }
            if (x != 0)
                for (size_t j = 0; j+1 < section.size(); ++j)
                    quad(ren,left[j],right[j],right[j+1],left[j+1]);
            left = right;
        }
    }
    ren.popClipRect();
}
} // namespace switchu::menu::music
