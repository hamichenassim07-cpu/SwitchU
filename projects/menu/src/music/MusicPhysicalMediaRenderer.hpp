#pragma once

#include <nxui/core/Types.hpp>
#include <nxui/core/Texture.hpp>

#include <array>
#include <string>

namespace nxui { class Renderer; class Font; }

namespace switchu::menu::music {

struct PhysicalMediaPose {
    nxui::Rect rect{};
    nxui::Color accent{0.54f, 0.72f, 1.0f, 1.0f};
    nxui::Color spine{0.10f, 0.12f, 0.16f, 1.0f};
    nxui::Color back{0.035f, 0.040f, 0.052f, 1.0f};
    float yawDeg = 0.f;
    float pitchDeg = 0.f;
    float rollDeg = 0.f;
    float depthPx = 6.f;
    float zLiftPx = 0.f;
    float vinylReveal = 0.f;   // 0..1, fraction of the disc exposed to the right.
    float vinylSpinRad = 0.f;
    float vinylLagPx = 0.f;    // tiny parallax delay relative to the sleeve.
    float alpha = 1.f;
    int detailLevel = 2;       // 0 neighbour, 1 normal, 2 selected/detail.
    bool selected = false;
    bool playing = false;
    // V8.6 label data is rendered from already-known metadata.  No artwork
    // decode or extra texture is needed to make the record album-specific.
    nxui::Font* vinylLabelFont = nullptr;
    const std::string* vinylLabelTitle = nullptr;
    const std::string* vinylLabelArtist = nullptr;
};

struct PhysicalMediaGeometry {
    nxui::Vec2 front[4]{}; // TL, TR, BR, BL after 3D projection.
    nxui::Vec2 vinylCenter{};
    float vinylRadius = 0.f;
    std::array<nxui::Vec2,32> vinylOutline{};
    int vinylOutlineCount = 0;
};

// Lightweight real 3D geometry projected through the existing 2D renderer.
// The sleeve is a six-face thin box and the record is a thin circular disc.
PhysicalMediaGeometry drawAlbumPhysicalMedia(nxui::Renderer& ren,
                                             nxui::Texture* cover,
                                             nxui::Texture* backCover,
                                             const PhysicalMediaPose& pose);

// Playlists use a small stack of sleeves. The leading sleeve is the real cover;
// backing sleeves deliberately remain neutral physical layers rather than HOME cards.
PhysicalMediaGeometry drawPlaylistPhysicalMedia(nxui::Renderer& ren,
                                                nxui::Texture* cover,
                                                nxui::Texture* backCover,
                                                const PhysicalMediaPose& pose);

// Short floor-only reflection for the physical object. It is always clipped to
// floorClip and therefore can never leak into the upper Music background.
void drawPhysicalMediaReflection(nxui::Renderer& ren,
                                 nxui::Texture* cover,
                                 const PhysicalMediaGeometry& geometry,
                                 const nxui::Rect& floorClip,
                                 float alpha,
                                 bool includeVinyl,
                                 const nxui::Color& accent);

} // namespace switchu::menu::music
