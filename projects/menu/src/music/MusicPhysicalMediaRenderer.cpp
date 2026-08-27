#include "MusicPhysicalMediaRenderer.hpp"

#include <nxui/core/Font.hpp>
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

struct CircleLut {
    int segments=0;
    std::array<float,41> c{};
    std::array<float,41> s{};
};

CircleLut makeCircleLut(int segments) {
    CircleLut out{};
    out.segments=segments;
    for (int i=0;i<=segments;++i) {
        const float a=2.f*kPi*static_cast<float>(i)/static_cast<float>(segments);
        out.c[static_cast<size_t>(i)]=std::cos(a);
        out.s[static_cast<size_t>(i)]=std::sin(a);
    }
    return out;
}

const CircleLut& circleLut(int segments) {
    static const CircleLut k20=makeCircleLut(20);
    static const CircleLut k24=makeCircleLut(24);
    static const CircleLut k28=makeCircleLut(28);
    static const CircleLut k32=makeCircleLut(32);
    static const CircleLut k40=makeCircleLut(40);
    switch (segments) {
        case 20: return k20;
        case 24: return k24;
        case 28: return k28;
        case 32: return k32;
        case 40: return k40;
        default: return k24;
    }
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

nxui::Vec2 projectDiscPointCS(float c, float sn, float radius,
                              float localCx, float localCy, float z,
                              const PhysicalMediaPose& pose,
                              const RotationBasis& basis) {
    const float cx = pose.rect.x + pose.rect.width * 0.5f;
    const float cy = pose.rect.y + pose.rect.height * 0.5f;
    const V3 rotated=basis.apply({localCx + c * radius,
                                  localCy + sn * radius,
                                  z});
    return project(rotated, cx, cy, pose.zLiftPx);
}

nxui::Vec2 projectDiscAngle(float angle, float radius,
                            float localCx, float localCy, float z,
                            const PhysicalMediaPose& pose,
                            const RotationBasis& basis) {
    return projectDiscPointCS(std::cos(angle),std::sin(angle),radius,
                              localCx,localCy,z,pose,basis);
}

void drawDiscFan(nxui::Renderer& ren, const PhysicalMediaPose& pose,
                 float localCx, float localCy, float radius, float z,
                 const nxui::Color& color, int segments,
                 const RotationBasis& basis) {
    const float cx = pose.rect.x + pose.rect.width * 0.5f;
    const float cy = pose.rect.y + pose.rect.height * 0.5f;
    const nxui::Vec2 center = project(basis.apply({localCx,localCy,z}),cx,cy,pose.zLiftPx);
    const auto& lut=circleLut(segments);
    nxui::Vec2 prev = projectDiscPointCS(lut.c[0],lut.s[0],radius,localCx,localCy,z,pose,basis);
    for (int i=1;i<=segments;++i) {
        const nxui::Vec2 cur = projectDiscPointCS(lut.c[static_cast<size_t>(i)],
                                                   lut.s[static_cast<size_t>(i)],
                                                   radius,localCx,localCy,z,pose,basis);
        ren.drawTriangle(center,prev,cur,color);
        prev=cur;
    }
}

void drawVinyl(nxui::Renderer& ren, nxui::Texture* cover,
               const PhysicalMediaPose& pose,
               float reveal, float spin, PhysicalMediaGeometry& geo) {
    (void)cover;
    reveal = std::clamp(reveal,0.f,1.f);
    if (reveal <= 0.001f) return;

    // Early builds left most of the record hidden behind the sleeve on hardware.
    // V8.7 keeps V8.6's convincing size/overlap and focuses on material realism
    // but gives the record the overlap visible in the supplied Album/Player
    // concepts: clearly present, still physically connected to the sleeve.
    const float radius = pose.rect.height * 0.478f;
    const float localCx = pose.rect.width * 0.5f - radius +
                          (radius * 1.32f) * reveal + pose.vinylLagPx;
    const float localCy = 0.f;
    const float zFront = -pose.depthPx*0.58f;
    const float zBack = zFront - std::max(1.5f,pose.depthPx*0.24f);
    const float a = pose.alpha;
    const int segs = pose.detailLevel >= 2 ? 40 : (pose.detailLevel == 1 ? 28 : 20);
    const auto& lut=circleLut(segs);
    const RotationBasis basis=rotationBasis(pose.yawDeg*kDeg,
                                             pose.pitchDeg*kDeg,
                                             pose.rollDeg*kDeg);

    // A restrained neutral rim separates black vinyl from the black Music
    // background without turning it into a glowing object.
    drawDiscFan(ren,pose,localCx,localCy,radius*1.018f,zBack-0.25f,
                {0.34f,0.36f,0.41f,0.070f*a},segs,basis);

    if (pose.detailLevel >= 1) {
        for (int i=0;i<segs;++i) {
            const size_t i0=static_cast<size_t>(i), i1=static_cast<size_t>(i+1);
            const nxui::Vec2 p0=projectDiscPointCS(lut.c[i0],lut.s[i0],radius,localCx,localCy,zBack,pose,basis);
            const nxui::Vec2 p1=projectDiscPointCS(lut.c[i1],lut.s[i1],radius,localCx,localCy,zBack,pose,basis);
            const nxui::Vec2 p2=projectDiscPointCS(lut.c[i1],lut.s[i1],radius,localCx,localCy,zFront,pose,basis);
            const nxui::Vec2 p3=projectDiscPointCS(lut.c[i0],lut.s[i0],radius,localCx,localCy,zFront,pose,basis);
            const float angularLight = 0.5f + 0.5f * (0.72f * lut.c[i0] - 0.28f * lut.s[i0]);
            const float edgeLift = 0.82f + 0.34f * angularLight;
            drawQuad(ren,{p0,p1,p2,p3},
                     {0.020f*edgeLift,0.022f*edgeLift,0.028f*edgeLift,0.98f*a});
        }
    }

    drawDiscFan(ren,pose,localCx,localCy,radius,zFront,
                {0.046f,0.049f,0.058f,0.998f*a},segs,basis);
    drawDiscFan(ren,pose,localCx,localCy,radius*0.965f,zFront+0.08f,
                {0.031f,0.034f,0.041f,0.995f*a},segs,basis);
    // Two broad tonal zones create a gentle pressed-surface curvature without
    // a new texture or post-process. The disc remains black/anthracite.
    drawDiscFan(ren,pose,localCx,localCy,radius*0.885f,zFront+0.10f,
                {0.038f,0.041f,0.049f,0.46f*a},segs,basis);
    drawDiscFan(ren,pose,localCx,localCy,radius*0.690f,zFront+0.12f,
                {0.028f,0.031f,0.038f,0.50f*a},segs,basis);

    const float discLight=faceLight(pose,{0.f,0.f,1.f},0.30f);
    const int grooveRings = pose.detailLevel >= 2 ? 7 : (pose.detailLevel == 1 ? 4 : 0);
    const float grooveAlpha=(0.060f+0.085f*std::clamp((discLight-0.30f)/0.70f,0.f,1.f))*a;
    for (int ring=0;ring<grooveRings;++ring) {
        const float rr=radius*(0.48f+0.061f*ring);
        nxui::Vec2 prev=projectDiscPointCS(lut.c[0],lut.s[0],rr,localCx,localCy,zFront+0.15f,pose,basis);
        for (int i=1;i<=segs;++i) {
            const size_t li=static_cast<size_t>(i);
            const nxui::Vec2 cur=projectDiscPointCS(lut.c[li],lut.s[li],rr,localCx,localCy,zFront+0.15f,pose,basis);
            const float ringVariation = (ring % 2 == 0) ? 1.0f : 0.72f;
            const float ringWidth = (ring % 3 == 0) ? 0.90f : 0.72f;
            ren.drawLine(prev,cur,{0.62f,0.66f,0.74f,
                         grooveAlpha*(0.70f+0.030f*ring)*ringVariation},ringWidth);
            prev=cur;
        }
    }

    // Stronger outer definition + three studio-highlight arcs. These are short
    // reflections, not a full bright outline, so the record remains black.
    {
        nxui::Vec2 prev=projectDiscPointCS(lut.c[0],lut.s[0],radius*0.995f,
                                           localCx,localCy,zFront+0.22f,pose,basis);
        for (int i=1;i<=segs;++i) {
            const size_t li=static_cast<size_t>(i);
            const nxui::Vec2 cur=projectDiscPointCS(lut.c[li],lut.s[li],radius*0.995f,
                                                     localCx,localCy,zFront+0.22f,pose,basis);
            ren.drawLine(prev,cur,{0.72f,0.76f,0.84f,0.23f*a},0.96f);
            prev=cur;
        }
    }
    if (pose.detailLevel >= 1) {
        struct ArcSpec { float rr, begin, finish, alpha, width; };
        const std::array<ArcSpec,3> arcs = {{
            {0.90f, -1.24f, -0.10f, 0.145f, 1.05f},
            {0.78f, -0.96f,  0.05f, 0.090f, 0.92f},
            {0.63f,  2.18f,  2.92f, 0.055f, 0.78f},
        }};
        constexpr int arcSegs=14;
        for (const auto& spec : arcs) {
            const float rr=radius*spec.rr;
            nxui::Vec2 prev=projectDiscAngle(spec.begin,rr,localCx,localCy,zFront+0.30f,pose,basis);
            for (int i=1;i<=arcSegs;++i) {
                const float t=static_cast<float>(i)/static_cast<float>(arcSegs);
                const float ang=spec.begin+(spec.finish-spec.begin)*t;
                const nxui::Vec2 cur=projectDiscAngle(ang,rr,localCx,localCy,zFront+0.30f,pose,basis);
                const float fade=std::sin(t*kPi);
                ren.drawLine(prev,cur,{0.91f,0.93f,0.98f,spec.alpha*fade*a},spec.width);
                prev=cur;
            }
        }
    }

    // The 0.30R label keeps real-LP proportions established in V8.6 without consuming the
    // grooved playing surface.
    const float labelR=radius*0.300f;
    const int labelSegments=pose.detailLevel>=2?32:(pose.detailLevel==1?24:20);
    drawDiscFan(ren,pose,localCx,localCy,labelR,zFront+0.4f,
                mix({0.055f,0.058f,0.068f,1.f},pose.accent,0.68f,0.995f*a),labelSegments,basis);

    // Printed geometric rings rotate with the record and give the centre a
    // designed pressing identity without sampling/decoding the artwork again.
    for (int band=0; band<2; ++band) {
        const float rr=labelR*(0.62f+0.18f*band);
        nxui::Vec2 prev=projectDiscAngle(spin,rr,localCx,localCy,zFront+0.56f,pose,basis);
        for (int i=1;i<=12;++i) {
            const float ang=spin+2.f*kPi*static_cast<float>(i)/12.f;
            const nxui::Vec2 cur=projectDiscAngle(ang,rr,localCx,localCy,zFront+0.56f,pose,basis);
            ren.drawLine(prev,cur,{1.f,1.f,1.f,(0.042f+0.020f*band)*a},0.65f);
            prev=cur;
        }
    }

    if (pose.detailLevel >= 1) {
        const float guideR=labelR*0.92f;
        nxui::Vec2 prev=projectDiscPointCS(lut.c[0],lut.s[0],guideR,localCx,localCy,zFront+0.60f,pose,basis);
        for (int i=1;i<=segs;++i) {
            const size_t li=static_cast<size_t>(i);
            const nxui::Vec2 cur=projectDiscPointCS(lut.c[li],lut.s[li],guideR,localCx,localCy,zFront+0.60f,pose,basis);
            ren.drawLine(prev,cur,{1.f,1.f,1.f,0.18f*a},0.72f);
            prev=cur;
        }
        drawDiscFan(ren,pose,localCx,localCy,labelR*0.072f,zFront+0.66f,
                    {0.012f,0.013f,0.016f,1.f*a},20,basis);
        const float tickR0=labelR*0.52f, tickR1=labelR*0.82f;
        const nxui::Vec2 tick0=projectDiscAngle(spin,tickR0,localCx,localCy,zFront+0.70f,pose,basis);
        const nxui::Vec2 tick1=projectDiscAngle(spin,tickR1,localCx,localCy,zFront+0.70f,pose,basis);
        ren.drawLine(tick0,tick1,{1.f,1.f,1.f,0.34f*a},1.05f);
    }

    // Album title/artist are already known metadata, so they can be printed on
    // the label with zero artwork work.  Draw before the sleeve: the sleeve then
    // naturally occludes any hidden part of the record.  Text stays screen-
    // upright for 720p legibility while the surrounding ring/tick still spins.
    if (pose.detailLevel >= 2 && pose.vinylLabelFont &&
        (pose.vinylLabelTitle || pose.vinylLabelArtist)) {
        const float screenCx=pose.rect.x+pose.rect.width*.5f;
        const float screenCy=pose.rect.y+pose.rect.height*.5f;
        const nxui::Vec2 labelCenter=project(
            basis.apply({localCx,localCy,zFront+0.74f}),
            screenCx,screenCy,pose.zLiftPx);
        const float maxTextW=labelR*1.48f;
        auto fitLabelText=[&](const std::string& value, float scale) {
            if (pose.vinylLabelFont->measure(value).x*scale <= maxTextW) return value;
            const std::string ellipsis="…";
            std::string out=value;
            auto popUtf8Codepoint=[&](std::string& text) {
                if (text.empty()) return;
                size_t i=text.size()-1;
                while (i>0 && (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u)
                    --i;
                text.resize(i);
            };
            while (!out.empty() &&
                   pose.vinylLabelFont->measure(out+ellipsis).x*scale > maxTextW)
                popUtf8Codepoint(out);
            return out.empty() ? ellipsis : out+ellipsis;
        };
        auto drawLabelLine=[&](const std::string* value, float yOffset,
                               float targetScale, float minScale,
                               const nxui::Color& color) {
            if (!value || value->empty()) return;
            const float rawW=std::max(1.f,pose.vinylLabelFont->measure(*value).x);
            const float scale=std::max(minScale,std::min(targetScale,maxTextW/rawW));
            const std::string shown=fitLabelText(*value,scale);
            const float w=pose.vinylLabelFont->measure(shown).x*scale;
            const nxui::Rect clip{labelCenter.x-maxTextW*.5f,
                                  labelCenter.y+yOffset-8.f,maxTextW,18.f};
            ren.pushClipRect(clip);
            ren.drawText(shown,{labelCenter.x-w*.5f,labelCenter.y+yOffset},
                         pose.vinylLabelFont,color,scale);
            ren.popClipRect();
        };
        // Keep the label deliberately simple at 720p: album, artist, Side A.
        drawLabelLine(pose.vinylLabelTitle,-16.f,0.42f,0.30f,
                      {1.f,1.f,1.f,0.92f*a});
        drawLabelLine(pose.vinylLabelArtist,-1.f,0.34f,0.28f,
                      {1.f,1.f,1.f,0.76f*a});
        const std::string side="SIDE A";
        const float sideScale=0.27f;
        const float sideW=pose.vinylLabelFont->measure(side).x*sideScale;
        ren.drawText(side,{labelCenter.x-sideW*.5f,labelCenter.y+17.f},
                     pose.vinylLabelFont,{1.f,1.f,1.f,0.60f*a},sideScale);
    }

    const float screenCx=pose.rect.x+pose.rect.width*.5f;
    const float screenCy=pose.rect.y+pose.rect.height*.5f;
    const V3 cc=basis.apply({localCx,localCy,zFront});
    geo.vinylCenter=project(cc,screenCx,screenCy,pose.zLiftPx);
    geo.vinylRadius=radius;

    geo.vinylOutlineCount=pose.detailLevel>=2?32:(pose.detailLevel==1?24:16);
    const int outlineLutSegments=geo.vinylOutlineCount==16?20:geo.vinylOutlineCount;
    const auto& outlineLut=circleLut(outlineLutSegments);
    for (int i=0;i<geo.vinylOutlineCount;++i) {
        const int lutIndex = geo.vinylOutlineCount==16
            ? static_cast<int>(std::round(static_cast<float>(i)*20.f/16.f))
            : i;
        geo.vinylOutline[static_cast<size_t>(i)]=projectDiscPointCS(
            outlineLut.c[static_cast<size_t>(lutIndex)],
            outlineLut.s[static_cast<size_t>(lutIndex)],
            radius,localCx,localCy,zFront,pose,basis);
    }
}

PhysicalMediaGeometry drawAlbumImpl(nxui::Renderer& ren,
                                    nxui::Texture* cover,
                                    nxui::Texture* backCover,
                                    const PhysicalMediaPose& pose,
                                    bool playlist) {
    PhysicalMediaGeometry geo{};
    const float reveal = playlist ? pose.vinylReveal*0.58f : pose.vinylReveal;
    drawVinyl(ren,cover,pose,reveal,pose.vinylSpinRad,geo);

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
                                 bool includeVinyl,
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

    if (includeVinyl && geometry.vinylOutlineCount>=3) {
        constexpr float verticalCompression=0.22f;
        const auto mirrorPoint=[planeY](const nxui::Vec2& p) {
            return nxui::Vec2{p.x, planeY + std::max(0.f,planeY-p.y)*verticalCompression};
        };
        const nxui::Vec2 center=mirrorPoint(geometry.vinylCenter);
        for (int i=0;i<geometry.vinylOutlineCount;++i) {
            const int next=(i+1)%geometry.vinylOutlineCount;
            const nxui::Vec2 p0=mirrorPoint(geometry.vinylOutline[static_cast<size_t>(i)]);
            const nxui::Vec2 p1=mirrorPoint(geometry.vinylOutline[static_cast<size_t>(next)]);
            const float depth=std::clamp(((p0.y+p1.y)*0.5f-planeY)/std::max(1.f,height),0.f,1.f);
            const float fade=std::pow(1.f-depth,2.4f);
            ren.drawTriangle(center,p0,p1,{0.008f,0.010f,0.014f,alpha*0.043f*fade});
        }
    }

    ren.popClipRect();
}

} // namespace switchu::menu::music
