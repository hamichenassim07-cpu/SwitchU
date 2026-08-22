#pragma once
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>

namespace nxui { class Renderer; }

// V10.25 shared renderer for the stylised suspended/launch cartridge.
// The same visual primitive is used by launch, HOME suspended state and lockscreen.
class StylisedGameCartridge {
public:
    static void draw(nxui::Renderer& ren,
                     const nxui::Texture* artworkTex,
                     const nxui::Texture* frontShellTex,
                     const nxui::Texture* backShellTex,
                     float centerX,
                     float centerY,
                     float width,
                     float height,
                     float depth,
                     float radius,
                     float rotY,
                     float rotX,
                     float scale,
                     float artworkInset,
                     const nxui::Color& panelColor,
                     const nxui::Color& borderColor,
                     float alpha = 1.f);
};
