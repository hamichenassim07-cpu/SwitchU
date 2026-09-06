#include "MusicPhysicalMediaRenderer.hpp"

#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace switchu::menu::music {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.f;
constexpr float kFocal = 980.f;

struct V3 { float x=0.f, y=0.f, z=0.f; };

struct RotationBasis {
    float cy=1.f, sy=0.f, cx=1.f, sx=0.f, cz=1.f, sz=0.f;
    V3 apply(V3 p) const {
        V3 yRot{p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};
        V3 xRot{yRot.x, yRot.y * cx - yRot.z * sx, yRot.y * sx + yRot.z * cx};
        return {xRot.x * cz - xRot.y * sz,
                xRot.x * sz + xRot.y * cz,
                xRot.z};
    }
};

RotationBasis rotationBasis(float yaw, float pitch, float roll) {
    return {std::cos(yaw), std::sin(yaw),
            std::cos(pitch), std::sin(pitch),
            std::cos(roll), std::sin(roll)};
}

V3 rotatePoint(V3 p, float yaw, float pitch, float roll) {
    return rotationBasis(yaw,pitch,roll).apply(p);
}

V3 normalised(V3 v) {
    const float len=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
    if (len<=1e-6f) return {0.f,0.f,1.f};
    return {v.x/len,v.y/len,v.z/len};
}
float dot(V3 a,V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }

float faceLight(const PhysicalMediaPose& pose, V3 localNormal, float ambient=0.42f) {
    // Fixed upper-right/front studio source. Rotating the physical object now
    // changes edge/groove intensity naturally instead of using static catches.
    constexpr V3 rawLight{-0.42f,-0.58f,0.70f};
    const V3 light=normalised(rawLight);
    const V3 n=normalised(rotatePoint(localNormal,
                                     pose.yawDeg*kDeg,
                                     pose.pitchDeg*kDeg,
                                     pose.rollDeg*kDeg));
    return std::clamp(ambient+(1.f-ambient)*std::max(0.f,dot(n,light)),ambient,1.f);
}

nxui::Vec2 project(V3 p, float cx, float cy, float zLift) {
    p.z += zLift;
    const float denom = std::max(140.f, kFocal - p.z);
    const float s = kFocal / denom;
    return {cx + p.x * s, cy + p.y * s};
}

nxui::Color mul(const nxui::Color& c, float rgb, float a) {
    return {std::clamp(c.r * rgb, 0.f, 1.f),
            std::clamp(c.g * rgb, 0.f, 1.f),
            std::clamp(c.b * rgb, 0.f, 1.f),
            std::clamp(c.a * a, 0.f, 1.f)};
}

nxui::Color mix(const nxui::Color& a, const nxui::Color& b, float t, float alpha) {
    t = std::clamp(t, 0.f, 1.f);
    return {a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            alpha};
}

void drawQuad(nxui::Renderer& ren, const std::array<nxui::Vec2,4>& q, const nxui::Color& c) {
    ren.drawTriangle(q[0], q[1], q[2], c);
    ren.drawTriangle(q[0], q[2], q[3], c);
}

void drawTexturedQuad(nxui::Renderer& ren, nxui::Texture* tex,
                      const std::array<nxui::Vec2,4>& q,
                      const nxui::Color& tint,
                      float u0=0.f, float v0=0.f, float u1=1.f, float v1=1.f) {
    if (!tex || !tex->valid() || tex->descriptorSlot() < 0) {
        drawQuad(ren, q, {0.075f,0.080f,0.095f,tint.a});
        return;
    }
    const int slot = tex->descriptorSlot();
    ren.drawTexturedTriangle(slot, q[0], {u0,v0}, q[1], {u1,v0}, q[2], {u1,v1}, tint);
    ren.drawTexturedTriangle(slot, q[0], {u0,v0}, q[2], {u1,v1}, q[3], {u0,v1}, tint);
}

struct SleeveProjection {
    std::array<nxui::Vec2,4> front{};
    std::array<nxui::Vec2,4> back{};
};

SleeveProjection sleeveProjection(const PhysicalMediaPose& pose,
                                  float offsetX=0.f, float offsetY=0.f,
                                  float yawExtra=0.f) {
    const float w = pose.rect.width;
    const float h = pose.rect.height;
    const float d = std::max(2.f, pose.depthPx);
    const float cx = pose.rect.x + w * 0.5f + offsetX;
    const float cy = pose.rect.y + h * 0.5f + offsetY;
    const float yaw = (pose.yawDeg + yawExtra) * kDeg;
    const float pitch = pose.pitchDeg * kDeg;
    const float roll = pose.rollDeg * kDeg;

    const std::array<V3,4> f = {{{-w*.5f,-h*.5f,d*.5f},{w*.5f,-h*.5f,d*.5f},
                                 {w*.5f,h*.5f,d*.5f},{-w*.5f,h*.5f,d*.5f}}};
    const std::array<V3,4> b = {{{-w*.5f,-h*.5f,-d*.5f},{w*.5f,-h*.5f,-d*.5f},
                                 {w*.5f,h*.5f,-d*.5f},{-w*.5f,h*.5f,-d*.5f}}};
    SleeveProjection out{};
    for (int i=0;i<4;++i) {
        out.front[i] = project(rotatePoint(f[i],yaw,pitch,roll),cx,cy,pose.zLiftPx);
        out.back[i]  = project(rotatePoint(b[i],yaw,pitch,roll),cx,cy,pose.zLiftPx);
    }
    return out;
}

void drawSleeve(nxui::Renderer& ren, nxui::Texture* cover, nxui::Texture* backCover,
                const PhysicalMediaPose& pose,
                float offsetX=0.f, float offsetY=0.f, float yawExtra=0.f,
                float layerAlpha=1.f, bool textureFront=true) {
    const auto p = sleeveProjection(pose, offsetX, offsetY, yawExtra);
    const float a = pose.alpha * layerAlpha;
    const nxui::Color dark{0.025f,0.028f,0.034f,a};
    const nxui::Color baseSide{pose.spine.r,pose.spine.g,pose.spine.b,a};

    const float leftLight=faceLight(pose,{-1.f,0.f,0.f},0.32f);
    const float rightLight=faceLight(pose,{1.f,0.f,0.f},0.32f);
    const float topLight=faceLight(pose,{0.f,-1.f,0.f},0.36f);
    const float bottomLight=faceLight(pose,{0.f,1.f,0.f},0.26f);
    const nxui::Color left=mul(baseSide,0.56f+0.48f*leftLight,1.f);
    const nxui::Color right=mul(baseSide,0.60f+0.54f*rightLight,1.f);
    const nxui::Color top=mix(dark,mul(baseSide,0.68f+0.45f*topLight,1.f),0.78f,a);
    const nxui::Color bottom=mul(baseSide,0.42f+0.34f*bottomLight,1.f);

    // The real rear material is generated asynchronously and cached on SD.
    // Until it exists, use the album-derived smoked back colour only: no fake
    // multi-tap blur, no extra texture sampling in the frame renderer.
    if (textureFront && backCover && backCover->valid() && backCover->descriptorSlot() >= 0) {
        drawTexturedQuad(ren,backCover,p.back,{0.90f,0.92f,0.96f,0.78f*a});
        drawQuad(ren,p.back,{pose.back.r,pose.back.g,pose.back.b,0.38f*a});
    } else {
        drawQuad(ren, p.back, {pose.back.r,pose.back.g,pose.back.b,a});
    }

    drawQuad(ren, {p.back[0],p.front[0],p.front[3],p.back[3]}, left);
    drawQuad(ren, {p.front[1],p.back[1],p.back[2],p.front[2]}, right);
    drawQuad(ren, {p.back[0],p.back[1],p.front[1],p.front[0]}, top);
    drawQuad(ren, {p.front[3],p.front[2],p.back[2],p.back[3]}, bottom);

    if (textureFront) {
        if (cover && cover->valid() && cover->descriptorSlot() >= 0) {
            drawTexturedQuad(ren, cover, p.front, {1.f,1.f,1.f,a});
        } else {
            // A missing/unsupported cover must read as an intentional fallback,
            // not the nearly-black "empty panel" seen at the far left of the
            // V8.5 hardware capture.
            drawQuad(ren, p.front,
                     mix({0.055f,0.060f,0.072f,1.f}, pose.accent, 0.16f, a));
            const nxui::Vec2 c0{(p.front[0].x+p.front[2].x)*0.5f,
                                (p.front[0].y+p.front[2].y)*0.5f};
            ren.drawCircle(c0, std::max(8.f,pose.rect.width*0.045f),
                           {1.f,1.f,1.f,0.16f*a},24);
            ren.drawCircle(c0, std::max(6.f,pose.rect.width*0.032f),
                           {0.04f,0.045f,0.055f,0.94f*a},24);
        }
    } else
        drawQuad(ren, p.front, mix({0.055f,0.060f,0.072f,1.f}, pose.accent, 0.12f, a));

    // Orientation-aware studio catches. They strengthen only as the relevant
    // physical edge turns toward the fixed light source.
    const float topCatch=std::clamp((topLight-0.36f)/0.64f,0.f,1.f);
    const float rightCatch=std::clamp((rightLight-0.32f)/0.68f,0.f,1.f);
    ren.drawLine(p.front[0], p.front[1], {1.f,1.f,1.f,(0.025f+0.105f*topCatch)*a}, 1.f);
    ren.drawLine(p.front[1], p.front[2],
                 mix({1.f,1.f,1.f,1.f}, pose.accent, 0.20f,
                     (0.020f+0.115f*rightCatch)*a),1.f);
}

PhysicalMediaGeometry drawAlbumImpl(nxui::Renderer& ren,
                                    nxui::Texture* cover,
                                    nxui::Texture* backCover,
                                    const PhysicalMediaPose& pose,
                                    bool playlist) {
    PhysicalMediaGeometry geo{};

    if (playlist) {
        // Backing geometry is intentionally cheaper on neighbours.
        if (pose.detailLevel>=1)
            drawSleeve(ren,nullptr,nullptr,pose,-8.f,-5.f,-2.0f,0.46f,false);
        drawSleeve(ren,nullptr,nullptr,pose,7.f,-2.f,1.6f,0.62f,false);
    }
    drawSleeve(ren,cover,backCover,pose,0.f,0.f,0.f,1.f,true);

    const auto p=sleeveProjection(pose);
    for(int i=0;i<4;++i) geo.front[i]=p.front[i];
    return geo;
}

} // namespace

PhysicalMediaGeometry drawAlbumPhysicalMedia(nxui::Renderer& ren,
                                             nxui::Texture* cover,
                                             nxui::Texture* backCover,
                                             const PhysicalMediaPose& pose) {
    return drawAlbumImpl(ren,cover,backCover,pose,false);
}

PhysicalMediaGeometry drawPlaylistPhysicalMedia(nxui::Renderer& ren,
                                                nxui::Texture* cover,
                                                nxui::Texture* backCover,
                                                const PhysicalMediaPose& pose) {
    return drawAlbumImpl(ren,cover,backCover,pose,true);
}

void drawPhysicalMediaReflection(nxui::Renderer& ren,
                                 nxui::Texture* cover,
                                 const PhysicalMediaGeometry& geometry,
                                 const nxui::Rect& floorClip,
                                 float alpha,
                                 [[maybe_unused]] bool includeVinyl,
                                 const nxui::Color& accent) {
    if (alpha <= 0.001f) return;
    ren.pushClipRect(floorClip);

    const float bottomY=(geometry.front[2].y+geometry.front[3].y)*0.5f;
    const float planeY=std::max(floorClip.y+2.f,bottomY+5.f);
    const float leftX=geometry.front[3].x;
    const float rightX=geometry.front[2].x;
    const float topLeftX=geometry.front[0].x;
    const float topRightX=geometry.front[1].x;
    const float height=std::clamp(std::abs(geometry.front[3].y-geometry.front[0].y)*0.235f,34.f,82.f);

    if (cover && cover->valid() && cover->descriptorSlot()>=0) {
        const int slot=cover->descriptorSlot();
        constexpr int strips=12;
        for(int i=0;i<strips;++i) {
            const float t0=float(i)/strips, t1=float(i+1)/strips;
            const float y0=planeY+height*t0, y1=planeY+height*t1;
            const float fade=std::pow(1.f-t0,2.7f);
            const float xL0=leftX+(topLeftX-leftX)*0.18f*t0;
            const float xR0=rightX+(topRightX-rightX)*0.18f*t0;
            const float xL1=leftX+(topLeftX-leftX)*0.18f*t1;
            const float xR1=rightX+(topRightX-rightX)*0.18f*t1;
            const nxui::Color tint{1.f,1.f,1.f,alpha*0.18f*fade};
            ren.drawTexturedTriangle(slot,{xL0,y0},{0.f,1.f-t0*.25f},
                                     {xR0,y0},{1.f,1.f-t0*.25f},
                                     {xR1,y1},{1.f,1.f-t1*.25f},tint);
            ren.drawTexturedTriangle(slot,{xL0,y0},{0.f,1.f-t0*.25f},
                                     {xR1,y1},{1.f,1.f-t1*.25f},
                                     {xL1,y1},{0.f,1.f-t1*.25f},tint);
        }
    } else {
        ren.drawGradientRect({std::min(leftX,rightX),planeY,std::abs(rightX-leftX),height},
                             {accent.r,accent.g,accent.b,alpha*0.065f},
                             {accent.r,accent.g,accent.b,0.f});
    }

    ren.popClipRect();
}

} // namespace switchu::menu::music
