// Switch U HOME V8.0A - background contextuel statique par jeu.
// Aucun script d'injection : ce fichier est compile automatiquement par
// projects/menu/src/**.cpp et remplace seulement les deux methodes weak.
#define SWITCHU_V80_BACKGROUND_STRONG 1
#include "WaraWaraBackground.hpp"
#include "core/DebugLog.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

namespace {

std::atomic<uint64_t> g_selectedGameTitle{0};

constexpr float kPreviewDebounce = 0.35f;
constexpr float kPreviewFadeDuration = 0.32f;

float random01V80() {
    return (std::rand() % 1000) / 1000.f;
}

float wrapValueV80(float value, float minValue, float maxValue) {
    const float span = maxValue - minValue;
    if (span <= 0.f)
        return minValue;
    while (value < minValue)
        value += span;
    while (value > maxValue)
        value -= span;
    return value;
}

std::string titleIdHex(uint64_t titleId) {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(titleId));
    return buffer;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string previewPathFor(uint64_t titleId) {
    if (titleId == 0)
        return {};

    const std::string tid = titleIdHex(titleId);
    const std::string root = "sdmc:/config/SwitchU/game_previews/";

    // Dossier par jeu : format recommande.
    const std::string folder = root + tid + "/";
    const std::string candidates[] = {
        folder + "background.jpg",
        folder + "background.jpeg",
        folder + "background.png",
        // Format plat pratique pour les premiers tests.
        root + tid + ".jpg",
        root + tid + ".jpeg",
        root + tid + ".png",
    };

    for (const auto& candidate : candidates) {
        if (fileExists(candidate))
            return candidate;
    }
    return {};
}

nxui::Rect coverRect(const nxui::Texture& texture, const nxui::Rect& area) {
    if (!texture.valid() || texture.width() <= 0 || texture.height() <= 0)
        return area;

    const float texW = static_cast<float>(texture.width());
    const float texH = static_cast<float>(texture.height());
    const float scale = std::max(area.width / texW, area.height / texH);
    const float drawW = texW * scale;
    const float drawH = texH * scale;

    return {
        area.x + (area.width - drawW) * 0.5f,
        area.y + (area.height - drawH) * 0.5f,
        drawW,
        drawH
    };
}

float smoothStep01(float value) {
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

} // namespace

void WaraWaraBackground::notifySelectedGame(uint64_t titleId) {
    if (titleId != 0)
        g_selectedGameTitle.store(titleId, std::memory_order_relaxed);
}

void WaraWaraBackground::onUpdate(float dt) {
    // Comportement historique du fond conserve.
    m_time += dt;
    for (auto& s : m_shapes) {
        if (m_config.layout == Layout::Floating) {
            const float top = m_rect.y - s.size - 20.f;
            const float bottom = m_rect.y +
                ((m_rect.height > 1.f) ? m_rect.height : 720.f) + s.size + 20.f;
            const float left = m_rect.x;
            const float width = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
            s.pos.y -= s.speed * dt;
            s.pos.x += std::sin(m_time * 0.7f + s.phase) * s.wobble * dt;
            s.pos.x = wrapValueV80(s.pos.x,
                                   left - s.size,
                                   left + width + s.size);
            if (s.pos.y + s.size < top) {
                s.pos.y = bottom;
                s.pos.x = left + random01V80() * width;
            }
        }
        s.rotation += s.rotSpeed * dt;
    }

    // V8.0A : observer le jeu actuellement selectionne.
    const uint64_t selected =
        g_selectedGameTitle.load(std::memory_order_relaxed);

    if (selected != 0 && selected != m_previewRequestedTitle) {
        m_previewRequestedTitle = selected;
        m_previewStableTimer = 0.f;
        m_previewLoadPending = false;
        m_previewPendingTitle = 0;
        m_previewPendingPath.clear();
        DebugLog::log("[home-preview] focus -> %016llX",
                      static_cast<unsigned long long>(selected));
    }

    if (m_previewRequestedTitle != 0 &&
        m_previewRequestedTitle != m_previewResolvedTitle &&
        !m_previewLoadPending) {
        m_previewStableTimer += std::max(0.f, dt);

        if (m_previewStableTimer >= kPreviewDebounce) {
            m_previewStableTimer = 0.f;
            m_previewPendingTitle = m_previewRequestedTitle;
            m_previewPendingPath = previewPathFor(m_previewPendingTitle);

            if (!m_previewPendingPath.empty()) {
                // Le chargement GPU doit rester sur le thread de rendu.
                m_previewLoadPending = true;
                DebugLog::log("[home-preview] candidate %016llX -> %s",
                              static_cast<unsigned long long>(m_previewPendingTitle),
                              m_previewPendingPath.c_str());
            } else {
                // Aucun fichier : on revient proprement au background Switch U.
                m_previewResolvedTitle = m_previewPendingTitle;
                m_previewNextAvailable = false;
                m_previewNextTitle = 0;
                m_previewFade = 0.f;
                m_previewTransitioning = m_previewCurrentAvailable;
                DebugLog::log("[home-preview] no asset for %016llX -> theme fallback",
                              static_cast<unsigned long long>(m_previewPendingTitle));
            }
        }
    }

    if (m_previewTransitioning) {
        m_previewFade = std::min(
            1.f,
            m_previewFade + std::max(0.f, dt) / kPreviewFadeDuration
        );

        if (m_previewFade >= 1.f) {
            if (m_previewNextAvailable) {
                m_previewCurrent = std::move(m_previewNext);
                m_previewCurrentAvailable = true;
                m_previewResolvedTitle = m_previewNextTitle;
            } else {
                m_previewCurrent = nxui::Texture{};
                m_previewCurrentAvailable = false;
            }

            m_previewNext = nxui::Texture{};
            m_previewNextAvailable = false;
            m_previewNextTitle = 0;
            m_previewFade = 0.f;
            m_previewTransitioning = false;
        }
    }
}

void WaraWaraBackground::onRender(nxui::Renderer& ren) {
    // Base Switch U actuelle : elle reste toujours derriere le nouveau layer.
    ren.useShader(nxui::ShaderProgram::Gradient);
    nxui::FsUniforms fs = {};
    fs.useTexture = 0;
    fs.param1 = m_time;
    fs.extra[0] = m_accent.r;  fs.extra[1] = m_accent.g;
    fs.extra[2] = m_accent.b;  fs.extra[3] = m_accent.a;
    fs.extra[4] = m_secondary.r;  fs.extra[5] = m_secondary.g;
    fs.extra[6] = m_secondary.b;  fs.extra[7] = m_secondary.a;
    fs.extra[8]  = m_shapeColor.r * 2.f;
    fs.extra[9]  = m_shapeColor.g * 2.f;
    fs.extra[10] = m_shapeColor.b * 2.f;
    fs.extra[11] = m_shapeColor.a;
    ren.pushFsUniforms(fs);
    ren.drawRect(m_rect, nxui::Color::white());
    ren.flush();
    ren.useShader(nxui::ShaderProgram::Basic);

    if (m_backgroundImage.valid() && m_config.imageOpacity > 0.f) {
        ren.drawTexture(&m_backgroundImage,
                        backgroundImageRect(),
                        nxui::Color::white().withAlpha(
                            m_config.imageOpacity * m_opacity));
    }

    for (const auto& s : m_shapes)
        drawShapeWithSymmetry(ren, s);

    ren.flush();

    // Le chargement de la preview est execute ici, avec le GPU/Renderer valides.
    if (m_previewLoadPending) {
        m_previewLoadPending = false;
        m_previewNext = nxui::Texture{};

        const bool loaded = m_previewNext.loadFromFile(
            ren.gpu(), ren, m_previewPendingPath, 1280);

        if (loaded) {
            m_previewNextAvailable = true;
            m_previewNextTitle = m_previewPendingTitle;
            m_previewFade = 0.f;
            m_previewTransitioning = true;
            DebugLog::log("[home-preview] loaded %016llX",
                          static_cast<unsigned long long>(m_previewNextTitle));
        } else {
            m_previewNextAvailable = false;
            m_previewNextTitle = 0;
            m_previewResolvedTitle = m_previewPendingTitle;
            m_previewFade = 0.f;
            m_previewTransitioning = m_previewCurrentAvailable;
            DebugLog::log("[home-preview] load failed %016llX -> theme fallback",
                          static_cast<unsigned long long>(m_previewPendingTitle));
        }

        m_previewPendingPath.clear();
        m_previewPendingTitle = 0;
    }

    const nxui::Rect area = {
        m_rect.x,
        m_rect.y,
        (m_rect.width > 1.f) ? m_rect.width : 1280.f,
        (m_rect.height > 1.f) ? m_rect.height : 720.f
    };

    float previewVisualAlpha = 0.f;

    if (m_previewTransitioning) {
        const float t = smoothStep01(m_previewFade);

        if (m_previewCurrentAvailable && m_previewCurrent.valid()) {
            const float a = 1.f - t;
            if (a > 0.001f) {
                ren.drawTexture(&m_previewCurrent,
                                coverRect(m_previewCurrent, area),
                                nxui::Color::white().withAlpha(a * m_opacity));
                previewVisualAlpha = std::clamp(previewVisualAlpha + a, 0.f, 1.f);
            }
        }

        if (m_previewNextAvailable && m_previewNext.valid()) {
            if (t > 0.001f) {
                ren.drawTexture(&m_previewNext,
                                coverRect(m_previewNext, area),
                                nxui::Color::white().withAlpha(t * m_opacity));
                previewVisualAlpha = std::clamp(previewVisualAlpha + t, 0.f, 1.f);
            }
        }
    } else if (m_previewCurrentAvailable && m_previewCurrent.valid()) {
        ren.drawTexture(&m_previewCurrent,
                        coverRect(m_previewCurrent, area),
                        nxui::Color::white().withAlpha(m_opacity));
        previewVisualAlpha = 1.f;
    }

    // Overlay cinematographique SEPARE logiquement de la texture de preview.
    // Haut presque intact, bas plus sombre pour garder les titres lisibles.
    // Il n'existe que lorsqu'une preview est effectivement visible.
    if (previewVisualAlpha > 0.001f) {
        const float a = std::clamp(previewVisualAlpha * m_opacity, 0.f, 1.f);
        ren.drawGradientRect(
            area,
            nxui::Color(0.006f, 0.008f, 0.018f, 0.06f * a),
            nxui::Color(0.004f, 0.004f, 0.012f, 0.76f * a)
        );
        ren.drawGradientRect(
            {area.x, area.y + area.height * 0.48f,
             area.width, area.height * 0.52f},
            nxui::Color(0.005f, 0.006f, 0.015f, 0.00f),
            nxui::Color(0.003f, 0.003f, 0.010f, 0.34f * a)
        );
    }

    ren.flush();
}
