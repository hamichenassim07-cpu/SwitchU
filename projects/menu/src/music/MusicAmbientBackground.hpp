#pragma once

#include "MusicAmbientPalette.hpp"

namespace nxui { class Renderer; }

namespace switchu::menu::music {
class MusicAmbientBackground {
public:
    void setPalette(const ambient::Palette& palette) { m_motion.setTarget(palette); }
    void update(float dt) { m_motion.update(dt); }
    void copyMotionFrom(const MusicAmbientBackground& other) { m_motion = other.m_motion; }
    void drawBase(nxui::Renderer& ren, float alpha = 1.f);
    void draw(nxui::Renderer& ren, float alpha, float floorY);

private:
    ambient::Motion m_motion;
    // Fixed storage, reused for all three layers. No owned GPU resources,
    // allocations, uploads or descriptor lifetime to manage on Music exit.
    std::array<ambient::RibbonColumn, ambient::kRibbonSegments + 1> m_columns{};
};
} // namespace switchu::menu::music
