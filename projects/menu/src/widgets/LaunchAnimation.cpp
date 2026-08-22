#include "LaunchAnimation.hpp"
#include <nxui/core/Renderer.hpp>
#include "../core/AudioManager.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

LaunchAnimation* LaunchAnimation::s_activeInstance = nullptr;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.f;
constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kSlotY = 662.f;
constexpr const char* kInsertSfxId = "launch_insert_v1021";
bool gInsertSfxLoaded = false;

struct V3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct Projected {
    nxui::Vec2 p{};
    float z = 0.f;
};

struct SideQuad {
    std::array<Projected, 4> p{};
    float z = 0.f;
    nxui::Color color{};
};

float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

float smooth01(float v) {
    v = clamp01(v);
    return v * v * (3.f - 2.f * v);
}

float easeOutCubic(float v) {
    v = clamp01(v);
    const float q = 1.f - v;
    return 1.f - q * q * q;
}

float lerpV10211(float a, float b, float t) {
    return a + (b - a) * t;
}

nxui::Color mixColor(const nxui::Color& a, const nxui::Color& b, float t) {
    t = clamp01(t);
    return {
        lerpV10211(a.r, b.r, t),
        lerpV10211(a.g, b.g, t),
        lerpV10211(a.b, b.b, t),
        lerpV10211(a.a, b.a, t)
    };
}

std::vector<V3> roundedOutline(float width,
                               float height,
                               float radius,
                               float z,
                               int cornerSegments = 5) {
    std::vector<V3> out;
    radius = std::clamp(radius, 0.f, std::min(width, height) * 0.5f);
    out.reserve(static_cast<size_t>((cornerSegments + 1) * 4));

    struct Corner { float cx, cy, a0; };
    const float hx = width * 0.5f;
    const float hy = height * 0.5f;
    const float quarter = kPi * 0.5f;
    const Corner corners[4] = {
        { hx - radius, -hy + radius, -quarter },
        { hx - radius,  hy - radius,  0.f      },
        {-hx + radius,  hy - radius,  quarter  },
        {-hx + radius, -hy + radius,  kPi      },
    };

    for (const auto& c : corners) {
        for (int i = 0; i <= cornerSegments; ++i) {
            const float a = c.a0 + quarter * (static_cast<float>(i) / cornerSegments);
            out.push_back({
                c.cx + std::cos(a) * radius,
                c.cy + std::sin(a) * radius,
                z
            });
        }
    }
    return out;
}

Projected projectPoint(const V3& src,
                       float centerX,
                       float centerY,
                       float rotY,
                       float rotX,
                       float scale) {
    const float cy = std::cos(rotY);
    const float sy = std::sin(rotY);
    const float cx = std::cos(rotX);
    const float sx = std::sin(rotX);

    const float x1 = src.x * cy + src.z * sy;
    const float z1 = -src.x * sy + src.z * cy;
    const float y2 = src.y * cx - z1 * sx;
    const float z2 = src.y * sx + z1 * cx;

    // Perspective projection on the Switch's 1280x720 plane. The geometry is
    // transformed in real 3D first; only the final projection is 2D.
    constexpr float focal = 860.f;
    const float denom = std::max(250.f, focal + z2);
    const float perspective = focal / denom;

    return {
        {centerX + x1 * perspective * scale,
         centerY + y2 * perspective * scale},
        z2
    };
}

std::vector<Projected> projectOutline(const std::vector<V3>& outline,
                                      float centerX,
                                      float centerY,
                                      float rotY,
                                      float rotX,
                                      float scale) {
    std::vector<Projected> out;
    out.reserve(outline.size());
    for (const auto& v : outline)
        out.push_back(projectPoint(v, centerX, centerY, rotY, rotX, scale));
    return out;
}

float averageDepth(const std::vector<Projected>& pts) {
    if (pts.empty()) return 0.f;
    float sum = 0.f;
    for (const auto& p : pts) sum += p.z;
    return sum / static_cast<float>(pts.size());
}

void drawSolidFan(nxui::Renderer& ren,
                  const std::vector<Projected>& pts,
                  const Projected& center,
                  const nxui::Color& color) {
    if (pts.size() < 3) return;
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto& a = pts[i];
        const auto& b = pts[(i + 1) % pts.size()];
        ren.drawTriangle(center.p, a.p, b.p, color);
    }
}

void drawTexturedFan(nxui::Renderer& ren,
                     int textureSlot,
                     const std::vector<V3>& model,
                     const std::vector<Projected>& pts,
                     const Projected& center,
                     float width,
                     float height,
                     const nxui::Color& tint) {
    if (textureSlot < 0 || model.size() != pts.size() || pts.size() < 3)
        return;

    const nxui::Vec2 uvCenter{0.5f, 0.5f};
    for (size_t i = 0; i < pts.size(); ++i) {
        const size_t j = (i + 1) % pts.size();
        const nxui::Vec2 uvA{
            clamp01(model[i].x / width + 0.5f),
            clamp01(model[i].y / height + 0.5f)
        };
        const nxui::Vec2 uvB{
            clamp01(model[j].x / width + 0.5f),
            clamp01(model[j].y / height + 0.5f)
        };
        ren.drawTexturedTriangle(textureSlot,
                                 center.p, uvCenter,
                                 pts[i].p, uvA,
                                 pts[j].p, uvB,
                                 tint);
    }
}


void drawTexturedFanMapped(nxui::Renderer& ren,
                           int textureSlot,
                           const std::vector<V3>& model,
                           const std::vector<Projected>& pts,
                           const Projected& center,
                           float modelCenterX,
                           float modelCenterY,
                           float modelWidth,
                           float modelHeight,
                           float u0,
                           float v0,
                           float u1,
                           float v1,
                           const nxui::Color& tint) {
    if (textureSlot < 0 || model.size() != pts.size() || pts.size() < 3 ||
        modelWidth <= 0.f || modelHeight <= 0.f)
        return;

    const nxui::Vec2 uvCenter{(u0 + u1) * 0.5f, (v0 + v1) * 0.5f};
    for (size_t i = 0; i < pts.size(); ++i) {
        const size_t j = (i + 1) % pts.size();

        const float ax = (model[i].x - modelCenterX) / modelWidth + 0.5f;
        const float ay = (model[i].y - modelCenterY) / modelHeight + 0.5f;
        const float bx = (model[j].x - modelCenterX) / modelWidth + 0.5f;
        const float by = (model[j].y - modelCenterY) / modelHeight + 0.5f;

        const nxui::Vec2 uvA{
            lerpV10211(u0, u1, clamp01(ax)),
            lerpV10211(v0, v1, clamp01(ay))
        };
        const nxui::Vec2 uvB{
            lerpV10211(u0, u1, clamp01(bx)),
            lerpV10211(v0, v1, clamp01(by))
        };

        ren.drawTexturedTriangle(textureSlot,
                                 center.p, uvCenter,
                                 pts[i].p, uvA,
                                 pts[j].p, uvB,
                                 tint);
    }
}

void computeCoverUv(const nxui::Texture* tex,
                    float destWidth,
                    float destHeight,
                    float& u0,
                    float& v0,
                    float& u1,
                    float& v1) {
    u0 = 0.f; v0 = 0.f; u1 = 1.f; v1 = 1.f;
    if (!tex || !tex->valid() || tex->width() <= 0 || tex->height() <= 0 ||
        destWidth <= 0.f || destHeight <= 0.f)
        return;

    const float srcAspect = static_cast<float>(tex->width()) /
                            static_cast<float>(tex->height());
    const float dstAspect = destWidth / destHeight;

    if (srcAspect > dstAspect) {
        const float visibleU = dstAspect / srcAspect;
        u0 = (1.f - visibleU) * 0.5f;
        u1 = 1.f - u0;
    } else if (srcAspect < dstAspect) {
        const float visibleV = srcAspect / dstAspect;
        v0 = (1.f - visibleV) * 0.5f;
        v1 = 1.f - v0;
    }
}

float texXToModel(float px, float width) {
    return (px / 1024.f - 0.5f) * width;
}

float texYToModel(float py, float height) {
    return (py / 1168.f - 0.5f) * height;
}

std::vector<V3> offsetOutline(std::vector<V3> model, float dx, float dy) {
    for (auto& v : model) {
        v.x += dx;
        v.y += dy;
    }
    return model;
}

struct ContactSlotPx {
    float x0, y0, x1, y1;
};

constexpr std::array<ContactSlotPx, 8> kCalibratedContactSlots{{
    {160.f, 806.f, 221.f, 1059.f},
    {251.f, 806.f, 312.f, 1060.f},
    {343.f, 806.f, 404.f, 1059.f},
    {435.f, 806.f, 495.f, 1060.f},
    {527.f, 806.f, 588.f, 1060.f},
    {618.f, 806.f, 680.f, 1060.f},
    {712.f, 806.f, 773.f, 1059.f},
    {803.f, 806.f, 864.f, 1060.f},
}};

void drawProjectedQuad(nxui::Renderer& ren,
                       const V3& a,
                       const V3& b,
                       const V3& c,
                       const V3& d,
                       float centerX,
                       float centerY,
                       float rotY,
                       float rotX,
                       float scale,
                       const nxui::Color& color) {
    const auto pa = projectPoint(a, centerX, centerY, rotY, rotX, scale);
    const auto pb = projectPoint(b, centerX, centerY, rotY, rotX, scale);
    const auto pc = projectPoint(c, centerX, centerY, rotY, rotX, scale);
    const auto pd = projectPoint(d, centerX, centerY, rotY, rotX, scale);
    ren.drawTriangle(pa.p, pb.p, pc.p, color);
    ren.drawTriangle(pa.p, pc.p, pd.p, color);
}

void drawProjectedLine(nxui::Renderer& ren,
                       const V3& a,
                       const V3& b,
                       float centerX,
                       float centerY,
                       float rotY,
                       float rotX,
                       float scale,
                       const nxui::Color& color,
                       float thickness) {
    const auto pa = projectPoint(a, centerX, centerY, rotY, rotX, scale);
    const auto pb = projectPoint(b, centerX, centerY, rotY, rotX, scale);
    ren.drawLine(pa.p, pb.p, color, thickness);
}

void drawCard3D(nxui::Renderer& ren,
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
                float alpha) {
    (void)artworkInset;
    (void)panelColor;
    (void)borderColor;

    alpha = clamp01(alpha);
    if (alpha <= 0.001f || width <= 1.f || height <= 1.f)
        return;

    // V10.23: keep the exact V10.20 physical geometry, but map the calibrated
    // front/back material assets onto it. 1024x1168 has almost exactly the
    // same aspect ratio as 286x326, so no visible shell distortion is needed.
    const float halfD = depth * 0.5f;
    const auto frontModel = roundedOutline(width, height, radius, -halfD);
    const auto backModel  = roundedOutline(width, height, radius,  halfD);
    const auto front = projectOutline(frontModel, centerX, centerY, rotY, rotX, scale);
    const auto back  = projectOutline(backModel, centerX, centerY, rotY, rotX, scale);
    const Projected frontCenter = projectPoint({0.f, 0.f, -halfD}, centerX, centerY, rotY, rotX, scale);
    const Projected backCenter  = projectPoint({0.f, 0.f,  halfD}, centerX, centerY, rotY, rotX, scale);

    // Orientation-aware base illumination. The detailed grain remains in the
    // PNG; these values only stop the object reading as a flat black sprite.
    const float frontLight = clamp01(0.50f + 0.50f * std::cos(rotY - 0.48f));
    const float backLight  = clamp01(0.50f + 0.50f * std::cos((rotY - kPi) - 0.48f));

    const nxui::Color frontBody{
        0.105f + 0.035f * frontLight,
        0.109f + 0.038f * frontLight,
        0.120f + 0.042f * frontLight,
        0.995f * alpha
    };
    const nxui::Color backBody{
        0.095f + 0.030f * backLight,
        0.099f + 0.033f * backLight,
        0.110f + 0.036f * backLight,
        0.995f * alpha
    };
    const nxui::Color sideBase{
        0.055f, 0.060f, 0.070f, 0.995f * alpha
    };

    std::vector<SideQuad> sides;
    sides.reserve(front.size());
    for (size_t i = 0; i < front.size(); ++i) {
        const size_t j = (i + 1) % front.size();
        SideQuad q;
        q.p = {front[i], front[j], back[j], back[i]};
        q.z = (front[i].z + front[j].z + back[j].z + back[i].z) * 0.25f;

        // Stronger edge response around profile angles makes the real depth
        // readable while the card spins.
        const float segmentPhase = static_cast<float>(i) /
                                   static_cast<float>(std::max<size_t>(1, front.size()));
        const float edgeLight = 0.5f + 0.5f *
            std::cos(segmentPhase * kTwoPi + rotY - 0.65f);
        const float profileBoost = std::pow(std::abs(std::sin(rotY)), 0.65f);
        q.color = mixColor(
            sideBase,
            nxui::Color(0.22f, 0.23f, 0.25f, 0.995f * alpha),
            clamp01(0.25f * edgeLight + 0.30f * profileBoost * edgeLight)
        );
        sides.push_back(q);
    }
    std::sort(sides.begin(), sides.end(), [](const SideQuad& a, const SideQuad& b) {
        return a.z > b.z;
    });

    const float frontDepth = averageDepth(front);
    const float backDepth = averageDepth(back);
    const bool frontNear = frontDepth < backDepth;

    // Calibrated artwork opening measured from the generated shell texture.
    // Values are normalized from the 1024x1168 authoring canvas so the mapping
    // remains correct at any rendered card size.
    constexpr float artPxX = 113.f;
    constexpr float artPxY = 153.f;
    constexpr float artPxW = 798.f;
    constexpr float artPxH = 791.f;
    constexpr float artPxRadius = 54.f;

    const float artCenterX =
        texXToModel(artPxX + artPxW * 0.5f, width);
    const float artCenterY =
        texYToModel(artPxY + artPxH * 0.5f, height);
    const float artW = width * (artPxW / 1024.f);
    const float artH = height * (artPxH / 1168.f);
    const float artR = std::max(2.f,
        0.5f * (width * (artPxRadius / 1024.f) +
                height * (artPxRadius / 1168.f)));

    auto drawArtwork = [&]() {
        // Slightly enlarge behind the transparent shell opening so texture
        // antialiasing can never reveal a one-pixel dark seam.
        const float pad = 1.4f;
        const float aw = artW + pad * 2.f;
        const float ah = artH + pad * 2.f;
        const float ar = artR + pad * 0.6f;
        const float artZ = -halfD + 0.10f;
        auto artModel = roundedOutline(aw, ah, ar, artZ, 6);
        artModel = offsetOutline(std::move(artModel), artCenterX, artCenterY);
        const auto artProj = projectOutline(artModel, centerX, centerY, rotY, rotX, scale);
        const Projected artCenter = projectPoint(
            {artCenterX, artCenterY, artZ},
            centerX, centerY, rotY, rotX, scale);

        if (artworkTex && artworkTex->valid()) {
            float u0, v0, u1, v1;
            computeCoverUv(artworkTex, aw, ah, u0, v0, u1, v1);
            drawTexturedFanMapped(
                ren,
                artworkTex->descriptorSlot(),
                artModel,
                artProj,
                artCenter,
                artCenterX,
                artCenterY,
                aw,
                ah,
                u0, v0, u1, v1,
                nxui::Color::white().withAlpha(alpha)
            );
        } else {
            drawSolidFan(ren, artProj, artCenter,
                         nxui::Color(0.18f, 0.21f, 0.27f, 0.98f * alpha));
        }
    };

    auto drawFrontReflection = [&]() {
        // A dynamic highlight is geometry, not painted into the texture. It
        // changes position/intensity from the real 3D rotation angle.
        const float spec = std::pow(
            clamp01(0.5f + 0.5f * std::cos(rotY - 0.62f)),
            5.5f
        );
        if (spec <= 0.01f)
            return;

        const float topLimit = texYToModel(artPxY, height);
        const float bottomStart = texYToModel(artPxY + artPxH, height);
        const float glide = 0.5f + 0.5f * std::sin(rotY - 0.35f);
        const float bandCx = lerpV10211(-width * 0.34f, width * 0.34f, glide);
        const float bandW = width * 0.075f;
        const float z = -halfD - 0.16f;
        const nxui::Color sheen(
            0.82f, 0.88f, 0.96f,
            (0.035f + 0.10f * spec) * alpha
        );

        // Top and bottom plastic only: never wash over the dynamic game icon.
        drawProjectedQuad(
            ren,
            {bandCx - bandW, -height * 0.5f + 4.f, z},
            {bandCx + bandW, -height * 0.5f + 4.f, z},
            {bandCx + bandW * 0.58f, topLimit - 2.f, z},
            {bandCx - bandW * 0.58f, topLimit - 2.f, z},
            centerX, centerY, rotY, rotX, scale, sheen
        );
        drawProjectedQuad(
            ren,
            {bandCx - bandW * 0.58f, bottomStart + 2.f, z},
            {bandCx + bandW * 0.58f, bottomStart + 2.f, z},
            {bandCx + bandW, height * 0.5f - 4.f, z},
            {bandCx - bandW, height * 0.5f - 4.f, z},
            centerX, centerY, rotY, rotX, scale, sheen
        );

        const nxui::Color rim(
            0.92f, 0.96f, 1.0f,
            (0.025f + 0.10f * spec) * alpha
        );
        for (size_t i = 0; i < front.size(); ++i)
            ren.drawLine(front[i].p, front[(i + 1) % front.size()].p, rim, 0.85f);
    };

    auto drawFront = [&]() {
        drawSolidFan(ren, front, frontCenter, frontBody);
        drawArtwork();

        if (frontShellTex && frontShellTex->valid()) {
            drawTexturedFan(
                ren,
                frontShellTex->descriptorSlot(),
                frontModel,
                front,
                frontCenter,
                width,
                height,
                nxui::Color::white().withAlpha(alpha)
            );
        } else {
            // Safe fallback if the ROMFS asset is absent.
            for (size_t i = 0; i < front.size(); ++i) {
                ren.drawLine(front[i].p, front[(i + 1) % front.size()].p,
                             nxui::Color(0.88f, 0.92f, 1.f, 0.08f * alpha), 0.9f);
            }
        }

        drawFrontReflection();
    };

    auto drawContacts = [&]() {
        const float backSpec = std::pow(
            clamp01(0.5f + 0.5f * std::cos((rotY - kPi) - 0.72f)),
            7.5f
        );
        const float zBase = halfD + 0.10f;

        for (const auto& slot : kCalibratedContactSlots) {
            const float x0 = texXToModel(slot.x0, width);
            const float x1 = texXToModel(slot.x1, width);
            const float y0 = texYToModel(slot.y0, height);
            const float y1 = texYToModel(slot.y1, height);
            const float sw = x1 - x0;

            const nxui::Color copper(
                0.42f + 0.11f * backSpec,
                0.215f + 0.065f * backSpec,
                0.055f + 0.020f * backSpec,
                0.99f * alpha
            );
            drawProjectedQuad(
                ren,
                {x0, y0, zBase},
                {x1, y0, zBase},
                {x1, y1, zBase},
                {x0, y1, zBase},
                centerX, centerY, rotY, rotX, scale, copper
            );

            const float inner = sw * 0.13f;
            const nxui::Color gold(
                0.78f + 0.18f * backSpec,
                0.54f + 0.26f * backSpec,
                0.10f + 0.18f * backSpec,
                0.96f * alpha
            );
            drawProjectedQuad(
                ren,
                {x0 + inner, y0 + 2.f, zBase + 0.06f},
                {x1 - inner, y0 + 2.f, zBase + 0.06f},
                {x1 - inner, y1 - 2.f, zBase + 0.06f},
                {x0 + inner, y1 - 2.f, zBase + 0.06f},
                centerX, centerY, rotY, rotX, scale, gold
            );

            // Narrow moving metal highlight.
            const float hX0 = x0 + sw * (0.20f + 0.34f * backSpec);
            const float hX1 = std::min(x1 - 1.f, hX0 + sw * 0.16f);
            drawProjectedQuad(
                ren,
                {hX0, y0 + 3.f, zBase + 0.10f},
                {hX1, y0 + 3.f, zBase + 0.10f},
                {hX1, y1 - 3.f, zBase + 0.10f},
                {hX0, y1 - 3.f, zBase + 0.10f},
                centerX, centerY, rotY, rotX, scale,
                nxui::Color(1.f, 0.90f, 0.50f,
                            (0.13f + 0.50f * backSpec) * alpha)
            );
        }
    };

    auto drawBackReflection = [&]() {
        const float spec = std::pow(
            clamp01(0.5f + 0.5f * std::cos((rotY - kPi) - 0.58f)),
            5.0f
        );
        if (spec <= 0.01f)
            return;

        const float glide = 0.5f + 0.5f * std::sin((rotY - kPi) - 0.25f);
        const float bandCx = lerpV10211(-width * 0.30f, width * 0.30f, glide);
        const float bandW = width * 0.09f;
        const float upperBottom = texYToModel(720.f, height);
        const float z = halfD + 0.18f;

        drawProjectedQuad(
            ren,
            {bandCx - bandW, -height * 0.5f + 5.f, z},
            {bandCx + bandW, -height * 0.5f + 5.f, z},
            {bandCx + bandW * 0.55f, upperBottom, z},
            {bandCx - bandW * 0.55f, upperBottom, z},
            centerX, centerY, rotY, rotX, scale,
            nxui::Color(0.82f, 0.88f, 0.96f,
                        (0.025f + 0.085f * spec) * alpha)
        );
    };

    auto drawBack = [&]() {
        drawSolidFan(ren, back, backCenter, backBody);

        // Metal is drawn first, then revealed by the calibrated transparent
        // contact slots in the rear shell texture.
        drawContacts();

        if (backShellTex && backShellTex->valid()) {
            drawTexturedFan(
                ren,
                backShellTex->descriptorSlot(),
                backModel,
                back,
                backCenter,
                width,
                height,
                nxui::Color::white().withAlpha(alpha)
            );
        } else {
            for (size_t i = 0; i < back.size(); ++i) {
                ren.drawLine(back[i].p, back[(i + 1) % back.size()].p,
                             nxui::Color(0.82f, 0.86f, 0.94f, 0.06f * alpha), 0.8f);
            }
        }

        drawBackReflection();
    };

    auto drawSides = [&]() {
        for (const auto& q : sides) {
            ren.drawTriangle(q.p[0].p, q.p[1].p, q.p[2].p, q.color);
            ren.drawTriangle(q.p[0].p, q.p[2].p, q.p[3].p, q.color);
        }
    };

    if (frontNear) {
        drawBack();
        drawSides();
        drawFront();
    } else {
        drawFront();
        drawSides();
        drawBack();
    }
}
}

void LaunchAnimation::ensureCartridgeTextures(nxui::Renderer& ren) {
    if (m_shellTexturesAttempted)
        return;

    m_shellTexturesAttempted = true;

    // Runtime assets are deliberately 512x584 (half the authoring size) to
    // keep GPU memory reasonable while retaining more detail than the ~286x326
    // on-screen card. maxSide=0 is mandatory: Texture::loadFromFile otherwise
    // downsizes to 128px by default.
    m_frontShellTexture.loadFromFile(
        ren.gpu(),
        ren,
        "romfs:/icons/launch_cartridge/cartridge_front_base.png",
        0
    );
    m_backShellTexture.loadFromFile(
        ren.gpu(),
        ren,
        "romfs:/icons/launch_cartridge/cartridge_back_base.png",
        0
    );
}

void LaunchAnimation::start(const nxui::Rect& from, const nxui::Texture* tex, float cornerRadius,
                            const nxui::Color& panelColor, const nxui::Color& borderColor,
                            uint64_t titleId, AccountUid uid,
                            LaunchCallback onLaunch, nxui::VoidCallback onDone)
{
    m_from         = from;
    m_tex          = tex;
    m_cornerRadius = cornerRadius;
    m_panelColor   = panelColor;
    m_borderColor  = borderColor;
    m_titleId      = titleId;
    m_uid          = uid;
    m_onLaunch     = std::move(onLaunch);
    m_onDone       = std::move(onDone);
    m_timer        = 0.f;
    m_playing      = true;
    m_launched     = false;
    m_doneCalled   = false;
    m_postLaunchHold = false;

    // Return to the original V10.20 card proportions: the HOME tile becomes a
    // slightly taller physical game card instead of a wide N64-like shape.
    constexpr float targetW = 286.f;
    constexpr float targetH = 326.f;
    const float startCx = from.x + from.width * 0.5f;
    const float startCy = from.y + from.height * 0.5f;
    m_target = {
        startCx - targetW * 0.5f,
        startCy - 24.f - targetH * 0.5f,
        targetW,
        targetH
    };

    m_insertSfxPlayed = false;
    if (!gInsertSfxLoaded) {
        if (auto* audio = AudioManager::active()) {
            audio->loadNamedSfx(
                kInsertSfxId,
                "romfs:/sounds/wiiu/sfx/launch_insert.wav",
                0.92f
            );
            gInsertSfxLoaded = true;
        }
    }

    s_activeInstance = this;
}

void LaunchAnimation::stop() {
    m_playing = false;
    m_tex = nullptr;
    clearGlobalStateIfOwned();
}

bool LaunchAnimation::globalPlaying() {
    return s_activeInstance && s_activeInstance->m_playing;
}

float LaunchAnimation::globalHudExitProgress() {
    if (!globalPlaying())
        return 0.f;
    return s_activeInstance->hudExitProgress();
}

float LaunchAnimation::hudExitProgress() const {
    return smooth01((m_timer - kHudExitStart) / kHudExitDur);
}

void LaunchAnimation::clearGlobalStateIfOwned() {
    if (s_activeInstance == this)
        s_activeInstance = nullptr;
}

void LaunchAnimation::onUpdate(float dt) {
    if (!m_playing) return;
    m_timer += dt;

    // Second animation SFX: mechanical insertion/click near the end of the
    // physical drop. The existing LaunchGame SFX is the flip/whoosh sound.
    const float insertClickMoment =
        kInsertStart + kInsertDur * 0.86f;
    if (!m_insertSfxPlayed && m_timer >= insertClickMoment) {
        m_insertSfxPlayed = true;
        if (auto* audio = AudioManager::active())
            audio->playNamedSfx(kInsertSfxId);
    }

    // Launch only after the insertion has fully completed and the screen has
    // reached black. The 3D animation therefore always has enough time to play.
    if (m_timer >= kLaunchMoment && !m_launched) {
        m_launched = true;
        if (m_titleId != 0 && m_onLaunch) {
            m_onLaunch(m_titleId, m_uid);
            m_postLaunchHold = true;
        } else if (m_onDone && !m_doneCalled) {
            // Resume/suspended-app path uses onDone instead of onLaunch. It
            // should receive the same fully-black launch timing.
            m_doneCalled = true;
            m_onDone();
            m_postLaunchHold = true;
        }
    }

    if (m_timer >= kTotalDur && !m_doneCalled) {
        m_doneCalled = true;
        if (m_onDone) {
            m_onDone();
            m_postLaunchHold = true;
        }
    }

    if (m_timer >= kTotalDur) {
        if (!m_postLaunchHold || m_timer >= kTotalDur + kPostLaunchBlackHold) {
            m_playing = false;
            clearGlobalStateIfOwned();
        }
    }
}

void LaunchAnimation::onRender(nxui::Renderer& ren) {
    if (!m_playing) return;

    ensureCartridgeTextures(ren);

    const float startCx = m_from.x + m_from.width * 0.5f;
    const float startCy = m_from.y + m_from.height * 0.5f;
    const float targetCx = m_target.x + m_target.width * 0.5f;
    const float targetCy = m_target.y + m_target.height * 0.5f;

    float centerX = startCx;
    float centerY = startCy;
    float cardW = m_from.width;
    float cardH = m_from.height;
    float depth = 1.f;
    float radius = m_cornerRadius;
    float rotY = 0.f;
    float rotX = 0.f;
    float scale = 1.f;
    float artworkInset = 8.f;
    float cardAlpha = 1.f;

    if (m_timer < kSpinStart) {
        const float p = smooth01(m_timer / kFormDur);
        centerX = lerpV10211(startCx, targetCx, p);
        centerY = lerpV10211(startCy, targetCy, p) - std::sin(clamp01(m_timer / kFormDur) * kPi) * 9.f;
        cardW = lerpV10211(m_from.width, m_target.width, p);
        cardH = lerpV10211(m_from.height, m_target.height, p);
        depth = lerpV10211(1.f, 18.f, p);
        radius = lerpV10211(m_cornerRadius, 22.f, p);
        artworkInset = lerpV10211(8.f, 11.f, p);
        scale = 1.f + 0.020f * std::sin(clamp01(m_timer / kFormDur) * kPi);
    } else if (m_timer < kSettleStart) {
        const float raw = (m_timer - kSpinStart) / kSpinDur;
        const float p = smooth01(raw);
        centerX = targetCx;
        centerY = targetCy - 7.f * std::sin(raw * kPi);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 18.f;
        radius = 22.f;
        artworkInset = 11.f;
        rotY = kTwoPi * p;
        rotX = -0.055f * std::sin(raw * kPi);
        scale = 1.f + 0.014f * std::sin(raw * kPi);
    } else if (m_timer < kInsertStart) {
        const float p = smooth01((m_timer - kSettleStart) / kSettleDur);
        centerX = targetCx;
        centerY = lerpV10211(targetCy - 2.f, targetCy, p);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 18.f;
        radius = 22.f;
        artworkInset = 11.f;
        rotY = kTwoPi;
        rotX = lerpV10211(-0.018f, 0.f, p);
    } else {
        const float insertRaw = clamp01((m_timer - kInsertStart) / kInsertDur);
        const float approachP = smooth01(std::min(1.f, insertRaw / 0.84f));
        const float clickP = smooth01(std::max(0.f, (insertRaw - 0.84f) / 0.16f));
        const float baseY = lerpV10211(targetCy, kSlotY + m_target.height * 0.60f, approachP);
        const float clickY = 8.f * clickP;
        centerX = targetCx;
        centerY = baseY + clickY;
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 18.f;
        radius = 22.f;
        artworkInset = 11.f;
        rotY = kTwoPi;
        rotX = 0.025f * approachP - 0.022f * clickP;
        scale = lerpV10211(1.f, 0.985f, approachP) - 0.010f * clickP;
    }

    const bool inserting = m_timer >= kInsertStart;
    const float insertP = inserting
        ? clamp01((m_timer - kInsertStart) / kInsertDur)
        : 0.f;

    if (inserting) {
        const float slotAlpha = smooth01(std::min(1.f, insertP * 4.f));
        const float slotW = 252.f;
        const float slotH = 14.f;
        const float slotX = kScreenW * 0.5f - slotW * 0.5f;

        ren.drawRoundedRect(
            {slotX - 18.f, kSlotY - 9.f, slotW + 36.f, slotH + 18.f},
            nxui::Color(0.f, 0.f, 0.f, 0.20f * slotAlpha),
            16.f
        );
        ren.drawRoundedRect(
            {slotX, kSlotY, slotW, slotH},
            nxui::Color(0.005f, 0.006f, 0.010f, 0.90f * slotAlpha),
            7.f
        );
        ren.drawRoundedRect(
            {slotX + 7.f, kSlotY + 1.4f, slotW - 14.f, 1.6f},
            nxui::Color(0.78f, 0.84f, 0.98f, 0.18f * slotAlpha),
            0.8f
        );
    }

    if (m_timer < kWipeStart) {
        drawCard3D(ren,
                   m_tex,
                   m_frontShellTexture.valid() ? &m_frontShellTexture : nullptr,
                   m_backShellTexture.valid() ? &m_backShellTexture : nullptr,
                   centerX,
                   centerY,
                   cardW,
                   cardH,
                   depth,
                   radius,
                   rotY,
                   rotX,
                   scale,
                   artworkInset,
                   m_panelColor,
                   m_borderColor,
                   cardAlpha);
    }

    if (inserting && m_timer < kWipeStart) {
        // Physical occlusion: the lower HOME block acts as the console shell.
        ren.drawRect(
            {0.f, kSlotY + 6.f, kScreenW, kScreenH - (kSlotY + 6.f)},
            nxui::Color(0.f, 0.f, 0.f, 0.985f)
        );

        const float slotAlpha = smooth01(std::min(1.f, insertP * 4.f));
        ren.drawRoundedRect(
            {kScreenW * 0.5f - 119.f, kSlotY + 1.2f, 238.f, 2.0f},
            nxui::Color(0.70f, 0.78f, 0.96f, 0.16f * slotAlpha),
            1.f
        );
    }

    // Simple classic circular wipe: no rim, no extra styling.
    if (m_timer >= kWipeStart) {
        const float raw = clamp01((m_timer - kWipeStart) / kWipeDur);
        const float p = smooth01(raw);
        const nxui::Vec2 wipeCenter{kScreenW * 0.5f, kSlotY + 5.f};
        const float radiusWipe = lerpV10211(6.f, 1040.f, p);

        ren.drawCircle(
            wipeCenter,
            radiusWipe,
            nxui::Color(0.f, 0.f, 0.f, 1.f),
            128
        );

        if (raw >= 0.995f) {
            ren.drawRect(
                {0.f, 0.f, kScreenW, kScreenH},
                nxui::Color(0.f, 0.f, 0.f, 1.f)
            );
        }
    }
}
