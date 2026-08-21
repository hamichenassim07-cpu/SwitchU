#include "LaunchAnimation.hpp"
#include <nxui/core/Renderer.hpp>
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
constexpr float kSlotY = 665.f;

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

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

nxui::Color mixColor(const nxui::Color& a, const nxui::Color& b, float t) {
    t = clamp01(t);
    return {
        lerp(a.r, b.r, t),
        lerp(a.g, b.g, t),
        lerp(a.b, b.b, t),
        lerp(a.a, b.a, t)
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
    alpha = clamp01(alpha);
    if (alpha <= 0.001f || width <= 1.f || height <= 1.f)
        return;

    const float halfD = depth * 0.5f;
    const auto frontModel = roundedOutline(width, height, radius, -halfD);
    const auto backModel  = roundedOutline(width, height, radius,  halfD);
    const auto front = projectOutline(frontModel, centerX, centerY, rotY, rotX, scale);
    const auto back  = projectOutline(backModel, centerX, centerY, rotY, rotX, scale);
    const Projected frontCenter = projectPoint({0.f, 0.f, -halfD}, centerX, centerY, rotY, rotX, scale);
    const Projected backCenter  = projectPoint({0.f, 0.f,  halfD}, centerX, centerY, rotY, rotX, scale);

    const nxui::Color darkBase{
        std::clamp(panelColor.r * 0.40f, 0.025f, 0.24f),
        std::clamp(panelColor.g * 0.43f, 0.028f, 0.26f),
        std::clamp(panelColor.b * 0.50f, 0.035f, 0.30f),
        0.985f * alpha
    };
    const nxui::Color frontBody = mixColor(darkBase, panelColor.withAlpha(1.f), 0.22f)
                                      .withAlpha(0.99f * alpha);
    const nxui::Color backBody{
        0.055f, 0.060f, 0.078f, 0.995f * alpha
    };
    const nxui::Color sideBase{
        0.025f, 0.030f, 0.043f, 0.99f * alpha
    };

    std::vector<SideQuad> sides;
    sides.reserve(front.size());
    for (size_t i = 0; i < front.size(); ++i) {
        const size_t j = (i + 1) % front.size();
        SideQuad q;
        q.p = {front[i], front[j], back[j], back[i]};
        q.z = (front[i].z + front[j].z + back[j].z + back[i].z) * 0.25f;
        const float edgeLight = 0.5f + 0.5f * std::cos(static_cast<float>(i) * 0.55f + rotY);
        q.color = mixColor(sideBase,
                           nxui::Color(0.12f, 0.14f, 0.19f, 0.99f * alpha),
                           0.24f * edgeLight);
        sides.push_back(q);
    }
    std::sort(sides.begin(), sides.end(), [](const SideQuad& a, const SideQuad& b) {
        return a.z > b.z; // farther first (positive z is farther from camera)
    });

    const float frontDepth = averageDepth(front);
    const float backDepth = averageDepth(back);
    const bool frontNear = frontDepth < backDepth;

    auto drawFront = [&]() {
        drawSolidFan(ren, front, frontCenter, frontBody);

        const float inset = std::clamp(artworkInset, 0.f, std::min(width, height) * 0.20f);
        const float artW = std::max(8.f, width - inset * 2.f);
        const float artH = std::max(8.f, height - inset * 2.f);
        const float artR = std::max(2.f, radius - inset * 0.62f);
        const float artZ = -halfD - 0.8f;
        const auto artModel = roundedOutline(artW, artH, artR, artZ);
        const auto art = projectOutline(artModel, centerX, centerY, rotY, rotX, scale);
        const Projected artCenter = projectPoint({0.f, 0.f, artZ}, centerX, centerY, rotY, rotX, scale);

        if (tex && tex->valid()) {
            drawTexturedFan(ren,
                            tex->descriptorSlot(),
                            artModel,
                            art,
                            artCenter,
                            artW,
                            artH,
                            nxui::Color::white().withAlpha(alpha));
        } else {
            drawSolidFan(ren, art, artCenter,
                         nxui::Color(0.18f, 0.22f, 0.30f, 0.96f * alpha));
        }

        // Thin physical lip around the artwork; this remains part of the 3D
        // front face, not a 2D fake rotation.
        const nxui::Color lip = borderColor.withAlpha(std::max(0.16f, borderColor.a) * 0.72f * alpha);
        for (size_t i = 0; i < art.size(); ++i) {
            const auto& a = art[i].p;
            const auto& b = art[(i + 1) % art.size()].p;
            ren.drawLine(a, b, lip, 1.1f);
        }
    };

    auto drawBack = [&]() {
        drawSolidFan(ren, back, backCenter, backBody);

        // A restrained inset gives the rear surface a recognisable physical
        // identity without copying a Nintendo cartridge design.
        const float insetW = width * 0.66f;
        const float insetH = height * 0.58f;
        const float insetR = std::max(5.f, radius * 0.68f);
        const float backZ = halfD + 0.7f;
        const auto insetModel = roundedOutline(insetW, insetH, insetR, backZ);
        const auto insetProj = projectOutline(insetModel, centerX, centerY, rotY, rotX, scale);
        const Projected insetCenter = projectPoint({0.f, 0.f, backZ}, centerX, centerY, rotY, rotX, scale);
        drawSolidFan(ren, insetProj, insetCenter,
                     nxui::Color(0.095f, 0.105f, 0.135f, 0.92f * alpha));

        const float markW = width * 0.22f;
        const float markH = std::max(12.f, height * 0.055f);
        const auto markModel = roundedOutline(markW, markH, markH * 0.5f, backZ + 0.3f, 3);
        const auto markProj = projectOutline(markModel, centerX, centerY, rotY, rotX, scale);
        const Projected markCenter = projectPoint({0.f, 0.f, backZ + 0.3f}, centerX, centerY, rotY, rotX, scale);
        drawSolidFan(ren, markProj, markCenter,
                     nxui::Color(0.52f, 0.58f, 0.72f, 0.24f * alpha));
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

    // The selected HOME cover is already centred horizontally. The physical
    // game card becomes a little narrower/taller and lifts above its slot.
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
        centerX = lerp(startCx, targetCx, p);
        centerY = lerp(startCy, targetCy, p) - std::sin(clamp01(m_timer / kFormDur) * kPi) * 10.f;
        cardW = lerp(m_from.width, m_target.width, p);
        cardH = lerp(m_from.height, m_target.height, p);
        depth = lerp(1.f, 18.f, p);
        radius = lerp(m_cornerRadius, 22.f, p);
        artworkInset = lerp(8.f, 11.f, p);
        scale = 1.f + 0.025f * std::sin(clamp01(m_timer / kFormDur) * kPi);
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
        centerY = lerp(targetCy - 2.f, targetCy, p);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 18.f;
        radius = 22.f;
        artworkInset = 11.f;
        rotY = kTwoPi;
        rotX = lerp(-0.018f, 0.f, p);
    } else {
        const float insertRaw = clamp01((m_timer - kInsertStart) / kInsertDur);
        const float p = insertRaw * insertRaw;
        centerX = targetCx;
        centerY = lerp(targetCy, kSlotY + m_target.height * 0.72f, p);
        cardW = m_target.width;
        cardH = m_target.height;
        depth = 18.f;
        radius = 22.f;
        artworkInset = 11.f;
        rotY = kTwoPi;
        rotX = 0.035f * p;
        scale = lerp(1.f, 0.965f, p);
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

    if (m_timer < kBlackStart) {
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

    if (inserting && m_timer < kBlackStart) {
        // Physical occlusion: once the card crosses the slot, the lower part is
        // hidden behind the HOME's black lower area instead of merely fading.
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

    if (m_timer >= kBlackStart) {
        const float blackP = clamp01((m_timer - kBlackStart) / kBlackDur);
        const float blackAlpha = smooth01(blackP);
        ren.drawRect({0.f, 0.f, kScreenW, kScreenH},
                     nxui::Color(0.f, 0.f, 0.f, blackAlpha));
    }
}
