#pragma once

#include "MusicAmbientPalette.hpp"
#include <nxui/core/Texture.hpp>

namespace nxui { class Renderer; }

namespace switchu::menu::music {
class MusicAmbientBackground {
public:
    void setPalette(const ambient::Palette& palette) { m_motion.setTarget(palette); }
    void update(float dt) { m_motion.update(dt); }
    void draw(nxui::Renderer& ren, float alpha, float floorY);

private:
    ambient::Motion m_motion;
    nxui::Texture m_lightTexture;
    bool m_textureAttempted = false;
};
} // namespace switchu::menu::music
