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

std::vector<V3> switchCardOutline(float width, float height, float z) {
    const float hx = width * 0.5f;
    const float hy = height * 0.5f;
    // Switch-card-inspired moulded silhouette: clipped corners rather than a
    // generic rounded glass rectangle. The asymmetry is deliberately subtle.
    return {
        {-hx + 18.f, -hy,        z},
        { hx - 16.f, -hy,        z},
        { hx,        -hy + 16.f, z},
        { hx,         hy - 13.f, z},
        { hx - 13.f,  hy,        z},
        {-hx + 15.f,  hy,        z},
        {-hx,         hy - 15.f, z},
        {-hx,        -hy + 18.f, z},
    };
}

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
                const nxui::Texture* tex,
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
    (void)radius;
    (void)artworkInset;
    alpha = clamp01(alpha);
    if (alpha <= 0.001f || width <= 1.f || height <= 1.f)
        return;

    const float halfD = depth * 0.5f;
    const auto frontModel = switchCardOutline(width, height, -halfD);
    const auto backModel  = switchCardOutline(width, height,  halfD);
    const auto front = projectOutline(frontModel, centerX, centerY, rotY, rotX, scale);
    const auto back  = projectOutline(backModel, centerX, centerY, rotY, rotX, scale);
    const Projected frontCenter = projectPoint({0.f, 0.f, -halfD}, centerX, centerY, rotY, rotX, scale);
    const Projected backCenter  = projectPoint({0.f, 0.f,  halfD}, centerX, centerY, rotY, rotX, scale);

    // Dense graphite plastic, with directional highlights rather than a flat
    // black frame. The front is slightly lighter than the rear shell.
    const float lightFacing = 0.5f + 0.5f * std::cos(rotY);
    const nxui::Color frontBody{
        0.040f + 0.020f * lightFacing,
        0.043f + 0.021f * lightFacing,
        0.050f + 0.024f * lightFacing,
        0.995f * alpha
    };
    const nxui::Color backBody{
        0.027f, 0.030f, 0.035f, 0.998f * alpha
    };
    const nxui::Color sideBase{
        0.020f, 0.022f, 0.027f, 0.995f * alpha
    };

    std::vector<SideQuad> sides;
    sides.reserve(front.size());
    for (size_t i = 0; i < front.size(); ++i) {
        const size_t j = (i + 1) % front.size();
        SideQuad q;
        q.p = {front[i], front[j], back[j], back[i]};
        q.z = (front[i].z + front[j].z + back[j].z + back[i].z) * 0.25f;
        const float edgeLight =
            0.5f + 0.5f * std::cos(static_cast<float>(i) * 0.82f + rotY);
        q.color = mixColor(
            sideBase,
            nxui::Color(0.115f, 0.122f, 0.140f, 0.995f * alpha),
            0.46f * edgeLight
        );
        sides.push_back(q);
    }
    std::sort(sides.begin(), sides.end(), [](const SideQuad& a, const SideQuad& b) {
        return a.z > b.z;
    });

    const float frontDepth = averageDepth(front);
    const float backDepth = averageDepth(back);
    const bool frontNear = frontDepth < backDepth;

    auto drawShellTexture = [&](float z, bool backFace) {
        // Fine moulding ribs / plastic grain. These are true projected 3D
        // lines on the physical shell, not a screen-space overlay.
        const nxui::Color dark =
            nxui::Color(0.005f, 0.006f, 0.008f, 0.22f * alpha);
        const nxui::Color light =
            nxui::Color(0.30f, 0.33f, 0.38f, 0.045f * alpha);
        const float usableW = width * 0.80f;
        const float usableH = height * 0.72f;
        for (int i = -3; i <= 3; ++i) {
            const float y = i * usableH / 8.f + (backFace ? -3.f : 3.f);
            drawProjectedLine(
                ren,
                {-usableW * 0.5f, y, z},
                { usableW * 0.5f, y + 3.f, z},
                centerX, centerY, rotY, rotX, scale,
                (i & 1) ? dark : light,
                0.65f
            );
        }
    };

    auto drawFront = [&]() {
        drawSolidFan(ren, front, frontCenter, frontBody);
        drawShellTexture(-halfD - 0.18f, false);

        // Front label bed.
        const float labelZ = -halfD - 0.65f;
        const float labelW = width * 0.80f;
        const float labelH = height * 0.80f;
        const float labelR = 9.f;
        const auto labelModel = roundedOutline(labelW, labelH, labelR, labelZ, 5);
        const auto labelProj = projectOutline(labelModel, centerX, centerY, rotY, rotX, scale);
        const auto labelCenter = projectPoint({0.f, 0.f, labelZ}, centerX, centerY, rotY, rotX, scale);
        drawSolidFan(
            ren, labelProj, labelCenter,
            nxui::Color(0.018f, 0.020f, 0.025f, 0.97f * alpha)
        );

        // Keep the source square artwork undistorted within the physical label.
        const float artSide = std::min(labelW * 0.86f, labelH * 0.91f);
        const float artZ = labelZ - 0.35f;
        const auto artModel = roundedOutline(artSide, artSide, 7.f, artZ, 5);
        const auto artProj = projectOutline(artModel, centerX, centerY, rotY, rotX, scale);
        const auto artCenter = projectPoint({0.f, 0.f, artZ}, centerX, centerY, rotY, rotX, scale);

        if (tex && tex->valid()) {
            drawTexturedFan(
                ren,
                tex->descriptorSlot(),
                artModel,
                artProj,
                artCenter,
                artSide,
                artSide,
                nxui::Color::white().withAlpha(alpha)
            );
        } else {
            drawSolidFan(
                ren, artProj, artCenter,
                nxui::Color(0.16f, 0.18f, 0.22f, 0.96f * alpha)
            );
        }

        // Moulded label lip and plastic highlight.
        const nxui::Color lip =
            borderColor.withAlpha(std::max(0.12f, borderColor.a) * 0.42f * alpha);
        for (size_t i = 0; i < labelProj.size(); ++i)
            ren.drawLine(labelProj[i].p, labelProj[(i + 1) % labelProj.size()].p, lip, 1.05f);

        drawProjectedLine(
            ren,
            {-width * 0.39f, -height * 0.42f, -halfD - 0.28f},
            { width * 0.34f, -height * 0.42f, -halfD - 0.28f},
            centerX, centerY, rotY, rotX, scale,
            nxui::Color(0.55f, 0.58f, 0.64f, 0.10f * alpha),
            1.0f
        );
    };

    auto drawBack = [&]() {
        drawSolidFan(ren, back, backCenter, backBody);
        drawShellTexture(halfD + 0.12f, true);

        const float surfaceZ = halfD + 0.72f;

        // Rear recessed PCB/contact bay.
        const float bayW = width * 0.76f;
        const float bayH = height * 0.60f;
        const float bayTop = -height * 0.11f;
        const float bayBottom = bayTop + bayH;
        drawProjectedQuad(
            ren,
            {-bayW * 0.5f, bayTop,    surfaceZ},
            { bayW * 0.5f, bayTop,    surfaceZ},
            { bayW * 0.5f, bayBottom, surfaceZ},
            {-bayW * 0.5f, bayBottom, surfaceZ},
            centerX, centerY, rotY, rotX, scale,
            nxui::Color(0.020f, 0.090f, 0.067f, 0.94f * alpha)
        );

        // Subtle PCB traces.
        const nxui::Color trace{0.57f, 0.43f, 0.14f, 0.30f * alpha};
        constexpr int contactCount = 8;
        const float contactsW = bayW * 0.82f;
        const float gap = contactsW / static_cast<float>(contactCount);
        const float padW = gap * 0.58f;
        const float padTop = height * 0.10f;
        const float padBottom = height * 0.39f;

        for (int i = 0; i < contactCount; ++i) {
            const float cx = -contactsW * 0.5f + gap * (i + 0.5f);
            const float traceEndY = -height * (0.06f + 0.015f * (i % 3));
            drawProjectedLine(
                ren,
                {cx, padTop - 2.f, surfaceZ + 0.10f},
                {cx + (i - 3.5f) * 2.0f, traceEndY, surfaceZ + 0.10f},
                centerX, centerY, rotY, rotX, scale,
                trace,
                1.05f
            );

            // Gold contacts with a darker copper base and a narrow specular strip.
            const float x0 = cx - padW * 0.5f;
            const float x1 = cx + padW * 0.5f;
            drawProjectedQuad(
                ren,
                {x0, padTop,    surfaceZ + 0.28f},
                {x1, padTop,    surfaceZ + 0.28f},
                {x1, padBottom, surfaceZ + 0.28f},
                {x0, padBottom, surfaceZ + 0.28f},
                centerX, centerY, rotY, rotX, scale,
                nxui::Color(0.74f, 0.49f, 0.10f, 0.98f * alpha)
            );
            drawProjectedQuad(
                ren,
                {x0 + padW * 0.16f, padTop + 3.f, surfaceZ + 0.34f},
                {x0 + padW * 0.36f, padTop + 3.f, surfaceZ + 0.34f},
                {x0 + padW * 0.36f, padBottom - 3.f, surfaceZ + 0.34f},
                {x0 + padW * 0.16f, padBottom - 3.f, surfaceZ + 0.34f},
                centerX, centerY, rotY, rotX, scale,
                nxui::Color(1.00f, 0.82f, 0.31f, 0.64f * alpha)
            );
        }

        // Small ICs and solder pads suggest the internal board while staying
        // restrained enough to read as a cartridge at normal HOME distance.
        drawProjectedQuad(
            ren,
            {-width * 0.14f, -height * 0.22f, surfaceZ + 0.18f},
            { width * 0.02f, -height * 0.22f, surfaceZ + 0.18f},
            { width * 0.02f, -height * 0.11f, surfaceZ + 0.18f},
            {-width * 0.14f, -height * 0.11f, surfaceZ + 0.18f},
            centerX, centerY, rotY, rotX, scale,
            nxui::Color(0.010f, 0.014f, 0.015f, 0.95f * alpha)
        );
        drawProjectedQuad(
            ren,
            { width * 0.07f, -height * 0.20f, surfaceZ + 0.18f},
            { width * 0.18f, -height * 0.20f, surfaceZ + 0.18f},
            { width * 0.18f, -height * 0.12f, surfaceZ + 0.18f},
            { width * 0.07f, -height * 0.12f, surfaceZ + 0.18f},
            centerX, centerY, rotY, rotX, scale,
            nxui::Color(0.015f, 0.020f, 0.020f, 0.92f * alpha)
        );

        // Moulded ribs around the contact bay.
        const nxui::Color rib{0.23f, 0.25f, 0.29f, 0.12f * alpha};
        drawProjectedLine(
            ren,
            {-bayW * 0.5f - 8.f, bayTop - 7.f, surfaceZ},
            { bayW * 0.5f + 8.f, bayTop - 7.f, surfaceZ},
            centerX, centerY, rotY, rotX, scale, rib, 1.2f
        );
        drawProjectedLine(
            ren,
            {-bayW * 0.5f - 8.f, bayBottom + 7.f, surfaceZ},
            { bayW * 0.5f + 8.f, bayBottom + 7.f, surfaceZ},
            centerX, centerY, rotY, rotX, scale, rib, 1.2f
        );
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

    // V10.21: morph the square HOME tile into a Switch-card-inspired
    // landscape cartridge so the rotation exposes a believable front/back.
    constexpr float targetW = 302.f;
    constexpr float targetH = 222.f;
    const float startCx = from.x + from.width * 0.5f;
    const float startCy = from.y + from.height * 0.5f;
    m_target = {
        startCx - targetW * 0.5f,
        startCy - 30.f - targetH * 0.5f,
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
        kInsertStart + kInsertDur * 0.76f;
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
        const float p = easeOutCubic(m_timer / kFormDur);
        centerX = lerpV10211(startCx, targetCx, p);
        centerY = lerpV10211(startCy, targetCy, p) - std::sin(clamp01(m_timer / kFormDur) * kPi) * 10.f;
        cardW = lerpV10211(m_from.width, m_target.width, p);
        cardH = lerpV10211(m_from.height, m_target.height, p);
        depth = lerpV10211(1.f, 15.f, p);
        radius = lerpV10211(m_cornerRadius, 10.f, p);
        artworkInset = lerpV10211(8.f, 10.f, p);
        scale = 1.f + 0.025f * std::sin(clamp01(m_timer / kFormDur) * kPi);
    } else if (m_timer < kSettleStart) {
        const float raw = (m_timer - kSpinStart) / kSpinDur;
        const float p = easeOutCubic(raw);
        centerX = targetCx;
        centerY = targetCy - 9.f * std::sin(raw * kPi);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 15.f;
        radius = 10.f;
        artworkInset = 10.f;
        rotY = kTwoPi * p;
        rotX = -0.070f * std::sin(raw * kPi);
        scale = 1.f + 0.020f * std::sin(raw * kPi);
    } else if (m_timer < kInsertStart) {
        const float p = smooth01((m_timer - kSettleStart) / kSettleDur);
        centerX = targetCx;
        centerY = lerpV10211(targetCy - 2.f, targetCy, p);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 15.f;
        radius = 10.f;
        artworkInset = 10.f;
        rotY = kTwoPi;
        rotX = lerpV10211(-0.018f, 0.f, p);
    } else {
        const float insertRaw = clamp01((m_timer - kInsertStart) / kInsertDur);
        const float p = insertRaw * insertRaw;
        centerX = targetCx;
        centerY = lerpV10211(targetCy, kSlotY + m_target.height * 0.72f, p);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 15.f;
        radius = 10.f;
        artworkInset = 10.f;
        rotY = kTwoPi;
        rotX = 0.035f * p;
        scale = lerpV10211(1.f, 0.965f, p);
    }

    const bool inserting = m_timer >= kInsertStart;
    const float insertP = inserting
        ? clamp01((m_timer - kInsertStart) / kInsertDur)
        : 0.f;

    if (inserting) {
        const float slotAlpha = smooth01(std::min(1.f, insertP * 4.f));
        const float slotW = 324.f;
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
            {kScreenW * 0.5f - 137.f, kSlotY + 0.8f, 274.f, 2.2f},
            nxui::Color(0.74f, 0.80f, 0.96f, 0.18f * slotAlpha),
            1.1f
        );
    }

    // V10.21: circular black wipe grows from the insertion point. A faint
    // cool rim gives the transition a polished "accepted by the console"
    // punctuation before the game process takes over.
    if (m_timer >= kWipeStart) {
        const float raw = clamp01((m_timer - kWipeStart) / kWipeDur);
        const float p = easeOutCubic(raw);
        const nxui::Vec2 wipeCenter{kScreenW * 0.5f, kSlotY + 5.f};
        const float radiusWipe = lerpV10211(8.f, 1040.f, p);
        const float rimAlpha = (1.f - raw) * 0.16f;

        ren.drawCircle(
            wipeCenter,
            radiusWipe + 14.f,
            nxui::Color(0.62f, 0.70f, 0.88f, rimAlpha),
            128
        );
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
