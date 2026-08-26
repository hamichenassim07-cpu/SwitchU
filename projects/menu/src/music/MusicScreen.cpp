#include "MusicScreen.hpp"
#include "MusicUiTiming.hpp"

#include "core/DebugLog.hpp"
#include "widgets/HomeCarouselStyle.hpp"
#include "widgets/HomeLiquidGlassStyle.hpp"
#include "widgets/HomeTypographyStyle.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace switchu::menu::music {
namespace {

constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kBottomY = 681.f;
constexpr float kFloorY = 506.f;
constexpr float kFloorH = kScreenH - kFloorY;
constexpr float kMusicCarouselBaselineY = switchu::homeui::kCarouselBaselineY + 28.f;
// HOME main-screen typography. 0.83 is the smallest permanent HOME scale
// used in the title/actions region; Music never renders readable text below it.
constexpr float kHomeMinTextScale = switchu::homeui::kMinimumMainTextScale;
const nxui::Color kMusicAccent {0.54f, 0.72f, 1.00f, 1.f};

// V0.02: Albums / Playlists are secondary Music views. The real HOME
// hierarchy stays visually present above them: Jeux / Applications / Musique.
constexpr int kRootCategoryCount = 2;

// Render helpers use this frame alpha so HOME <-> Music can cross-fade
// without rebuilding either side of the interface.
float gMusicUiAlpha = 1.f;
nxui::Color gMusicAccent = kMusicAccent;

std::string utf8Codepoint(uint32_t cp) {
    std::string out;
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

std::string buttonGlyph(nxui::Button b) {
    switch (b) {
        case nxui::Button::A: return utf8Codepoint(0xE0E0);
        case nxui::Button::B: return utf8Codepoint(0xE0E1);
        case nxui::Button::X: return utf8Codepoint(0xE0E2);
        case nxui::Button::Y: return utf8Codepoint(0xE0E3);
        case nxui::Button::L: return utf8Codepoint(0xE0E4);
        case nxui::Button::R: return utf8Codepoint(0xE0E5);
        case nxui::Button::ZL: return utf8Codepoint(0xE0E6);
        case nxui::Button::ZR: return utf8Codepoint(0xE0E7);
        case nxui::Button::Plus: return utf8Codepoint(0xE0F1);
        case nxui::Button::Minus: return utf8Codepoint(0xE0F2);
        default: return {};
    }
}

nxui::Color textPrimary(float a = 1.f) { return {0.965f, 0.972f, 0.985f, a * gMusicUiAlpha}; }
nxui::Color textSecondary(float a = 1.f) { return {0.66f, 0.69f, 0.74f, a * gMusicUiAlpha}; }
nxui::Color accent(float a = 1.f) {
    return {gMusicAccent.r, gMusicAccent.g, gMusicAccent.b, a * gMusicUiAlpha};
}
nxui::Color panel2(float a = 1.f) { return {0.105f, 0.116f, 0.138f, a * gMusicUiAlpha}; }

void drawSmokedGlassPanel(nxui::Renderer& ren, const nxui::Rect& rect,
                          float radius, float alphaValue = 1.f,
                          float accentStrength = 0.035f) {
    const nxui::LiquidGlassSettings oldGlass = ren.liquidGlassSettings();
    switchu::homeui::applyLiquidGlassV105(ren);
    ren.captureToOffscreen(true);
    ren.drawLiquidGlass(0, rect, radius,
                        {gMusicAccent.r, gMusicAccent.g, gMusicAccent.b, accentStrength},
                        0.42f * alphaValue * gMusicUiAlpha, 0.f);
    ren.drawRoundedRect(rect,
                        {0.020f,0.024f,0.032f,0.46f * alphaValue * gMusicUiAlpha},
                        radius);
    ren.drawRoundedRectOutline(rect,
                               {0.92f,0.95f,1.f,0.10f * alphaValue * gMusicUiAlpha},
                               radius, 1.f);
    ren.liquidGlassSettings() = oldGlass;
}

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float smoothStep(float v) {
    v = clamp01(v);
    return v * v * (3.f - 2.f * v);
}

bool statusFlag(const switchu::music::Status& st, uint32_t flag) {
    return (st.flags & flag) != 0;
}

std::string repeatLabel(uint8_t mode) {
    switch (static_cast<switchu::music::RepeatMode>(mode)) {
        case switchu::music::RepeatMode::Off: return "↻";
        case switchu::music::RepeatMode::Track: return "1";
        case switchu::music::RepeatMode::Queue: return "∞";
    }
    return "↻";
}

} // namespace

MusicScreen::MusicScreen() {
    setRect({0.f, 0.f, kScreenW, kScreenH});
    // V0.02 remains mounted while inactive so the daemon session can keep
    // owning HOME BGM without recreating Music UI/GPU state.
    setVisible(true);
    setOpacity(1.f);
    setFocusable(false);
    setFrameworkTouchEnabled(false);
    setTag("switchu_music_v002");
    setupActions();
}


MusicScreen::~MusicScreen() = default;

void MusicScreen::setupActions() {
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_modal != Modal::None) modalActivate(); else activateSelection();
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (m_modal != Modal::None) modalCancel(); else goBack();
    });
    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_modal == Modal::PlaylistNameKeyboard)
            modalBackspace();
        else if (m_modal == Modal::None)
            contextualX();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if (m_modal == Modal::None)
            contextualY();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        if (m_modal == Modal::PlaylistNameKeyboard)
            modalConfirm();
        else if (m_modal == Modal::None && m_view == View::Playlists && !contentTransitionBusy())
            openCreatePlaylist();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        if (m_modal == Modal::None)
            openNowPlaying();
    });

    // The HOME category capsule stays authoritative. From the Music root,
    // L moves naturally back to Applications. Albums/Playlists are a
    // secondary vertical choice instead of replacing the HOME capsule.
    addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        if (m_modal == Modal::None && (m_view == View::Albums || m_view == View::Playlists)) {
            if (!m_closing) {
                m_closing = true;
                setFocusable(false);
            }
        }
    });
    addAction(static_cast<uint64_t>(nxui::Button::R), []() {
        // Music is the right-most HOME category; R intentionally has no root action.
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZL), [this]() {
        if (m_modal == Modal::None && m_view == View::NowPlaying && !contentTransitionBusy())
            seekRelative(-10'000);
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZR), [this]() {
        if (m_modal == Modal::None && m_view == View::NowPlaying && !contentTransitionBusy())
            seekRelative(10'000);
    });

    // D-pad Up/Down exclusively owns Albums <-> Playlists at the Music root.
    // The left analog stick remains usable inside tracklists/controls, but can
    // never change the root category accidentally.
    addAction(static_cast<uint64_t>(nxui::Button::DUp), [this]() {
        if (m_modal != Modal::None) { modalMove(0, -1); return; }
        if (rootView()) { setRootCategory(rootCategoryIndex() - 1); return; }
        moveSelection(0, -1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::DDown), [this]() {
        if (m_modal != Modal::None) { modalMove(0, 1); return; }
        if (rootView()) { setRootCategory(rootCategoryIndex() + 1); return; }
        moveSelection(0, 1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::LStickU), [this]() {
        if (m_modal != Modal::None) { modalMove(0, -1); return; }
        if (!rootView()) moveSelection(0, -1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::LStickD), [this]() {
        if (m_modal != Modal::None) { modalMove(0, 1); return; }
        if (!rootView()) moveSelection(0, 1);
    });
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (m_modal != Modal::None) modalMove(-1, 0); else moveSelection(-1, 0);
    });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (m_modal != Modal::None) modalMove(1, 0); else moveSelection(1, 0);
    });
}

void MusicScreen::show() {
    DebugLog::log("[music-diag] OPEN_MUSIC begin");
    m_active = true;
    m_closing = false;
    m_transitionAlpha = 0.f;
    setVisible(true);
    setOpacity(1.f);
    setFocusable(true);
    m_view = View::Albums;
    m_nowPlayingReturnView = View::Albums;
    m_queueReturnView = View::NowPlaying;
    m_selection = 0;
    switchu::homeui::jumpCarouselTo(m_rootCarouselMotion, 0, rootItemCount());
    m_listVisualSelection = 0.f;
    m_detailTransition = 1.f;
    m_detailClosing = false;
    m_nowPlayingEnter = 1.f;
    m_nowPlayingClosing = false;
    m_nowPlayingReturnSelection = 0;
    m_queueReturnSelection = 0;
    m_rootInfoReveal = 1.f;
    m_rootCategoryTransition = 1.f;
    m_rootCategoryDirection = 1;
    m_rootCategoryHintTimer = 0.f;
    m_rootCategoryPreviousView = View::Albums;
    m_rootCategoryPreviousPosition = 0.f;
    m_rootCategoryPreviousSelection = 0;
    m_rootEntryBounceActive = true;
    m_rootEntryBounceTime = 0.f;
    m_modal = Modal::None;
    m_client.loadQueueTrackIds();
    refreshStatus(true);
    if (!m_hasScanned) startScan(false);
    DebugLog::log("[music-diag] OPEN_MUSIC ready scan=%d", m_scanRunning ? 1 : 0);
    DebugLog::log("[music-diag] V8.4 reference-ui artwork_worker=OFF secondary_decode=OFF single_decode_accent=ON physical_media=ON");
}


void MusicScreen::hide() {
    DebugLog::log("[music-diag] CLOSE_MUSIC hide session=%d cache=%zu",
                  hasMusicSession() ? 1 : 0, m_coverCache.size());
    m_active = false;
    m_closing = false;
    m_transitionAlpha = 0.f;
    setVisible(true);
    setOpacity(1.f);
    setFocusable(false);
    m_modal = Modal::None;
    // Never destroy Music GPU cover resources during the HOME handoff. The
    // bounded cache stays alive while Music is hidden so HOME restoration never
    // overlaps texture destruction from this surface.
}


void MusicScreen::startScan(bool force) {
    if (m_scanRunning || (m_hasScanned && !force)) return;
    DebugLog::log("[music-diag] SCAN_LIBRARY begin root=%s mode=safe-async", switchu::music::kRootDirectory);
    m_filesVisited.store(0, std::memory_order_relaxed);
    m_tracksFound.store(0, std::memory_order_relaxed);
    m_scanRunning = true;
    // Restore the last console-validated ownership model (V5/V7). A joinable
    // async task avoids a detached metadata worker surviving UI state changes.
    m_scanFuture = std::async(std::launch::async, [this]() {
        return MusicLibrary::scan(switchu::music::kRootDirectory,
                                  &m_filesVisited, &m_tracksFound, nullptr);
    });
}


void MusicScreen::finishScanIfReady() {
    if (!m_scanRunning || !m_scanFuture.valid()) return;
    if (m_scanFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    std::string selectedAlbumKey;
    std::string selectedPlaylistId;
    if (m_view == View::Albums && m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size())
        selectedAlbumKey = m_library.albums[static_cast<size_t>(m_selection)].key;
    if (m_view == View::Playlists && m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists().size())
        selectedPlaylistId = m_playlistStore.playlists()[static_cast<size_t>(m_selection)].id;

    DebugLog::log("[music-diag] SCAN_LIBRARY commit begin");
    m_library = m_scanFuture.get();
    m_scanRunning = false;
    m_hasScanned = true;
    m_playlistStore.load(m_library);
    m_playlistStore.pruneMissing(m_library);
    m_playlistStore.save();
    m_client.loadQueueTrackIds();
    m_coverCache.resetFailures();

    if (m_view == View::Albums && !selectedAlbumKey.empty()) {
        for (size_t i = 0; i < m_library.albums.size(); ++i) {
            if (m_library.albums[i].key == selectedAlbumKey) {
                m_selection = static_cast<int>(i);
                break;
            }
        }
    } else if (m_view == View::Playlists && !selectedPlaylistId.empty()) {
        const auto& playlists = m_playlistStore.playlists();
        for (size_t i = 0; i < playlists.size(); ++i) {
            if (playlists[i].id == selectedPlaylistId) {
                m_selection = static_cast<int>(i);
                break;
            }
        }
    }

    if (m_view == View::AlbumDetail && m_detailAlbum >= m_library.albums.size()) {
        m_view = View::Albums;
        m_selection = 0;
        m_detailTransition = 1.f;
        m_detailClosing = false;
    }
    if (m_view == View::PlaylistDetail && m_detailPlaylist >= m_playlistStore.playlists().size()) {
        m_view = View::Playlists;
        m_selection = 0;
        m_detailTransition = 1.f;
        m_detailClosing = false;
    }

    clampSelectionForView();
    if (rootView()) switchu::homeui::jumpCarouselTo(m_rootCarouselMotion, m_selection, rootItemCount());
    DebugLog::log("[music-diag] SCAN_LIBRARY done tracks=%zu albums=%zu artists=%zu",
                  m_library.tracks.size(), m_library.albums.size(), m_library.artists.size());
}


void MusicScreen::refreshStatus(bool force) {
    if (!force && m_statusTimer < 0.25f) return;
    m_statusTimer = 0.f;
    switchu::music::Status fresh{};
    if (m_client.getStatus(fresh)) {
        const uint64_t previousTrackId = m_status.track_id;
        const bool nextSoon = statusFlag(fresh, switchu::music::MusicStatus_NextSoon);
        if (nextSoon && !m_nextSoonWasVisible)
            m_nextToastTimer = 3.5f;
        m_nextSoonWasVisible = nextSoon;
        m_status = fresh;
        // Playback state changes never launch artwork decoding. This keeps GPU
        // and SD-card work away from the exact moment the daemon swaps decoders.
        if (fresh.track_id != previousTrackId) {
            DebugLog::log("[music-diag] STATUS track changed %016llX -> %016llX",
                          static_cast<unsigned long long>(previousTrackId),
                          static_cast<unsigned long long>(fresh.track_id));
            // A new record side gets a tiny, quickly decaying start impulse.
            // This is UI-only state and never touches decoder/audio timing.
            if (fresh.track_id != 0)
                m_vinylSpinBoost = 1.f;
        }

        const bool session = statusFlag(fresh, switchu::music::MusicStatus_SessionActive);
        const bool sessionChanged = session != m_lastSessionGuardState;
        if (sessionChanged) {
            m_lastSessionGuardState = session;
            DebugLog::log("[music-diag] SESSION state=%d playing=%d paused=%d",
                          session ? 1 : 0,
                          statusFlag(fresh, switchu::music::MusicStatus_Playing) ? 1 : 0,
                          statusFlag(fresh, switchu::music::MusicStatus_Paused) ? 1 : 0);
        }

        // The HOME BGM can be restarted by unrelated system surfaces
        // (profile/settings/lockscreen wake). Re-assert the Music guard on
        // every status poll while a real session exists. The callback is
        // idempotent: it only fades HOME audio if it actually restarted.
        if (m_sessionGuardCb && (session || sessionChanged))
            m_sessionGuardCb(session);
    } else {
        // Status IPC is not authoritative during a transient daemon/menu handoff.
        // The persisted session flag gives HOME BGM a safe fallback instead of
        // briefly restarting underneath an active Music session.
        std::error_code ec;
        const bool session = std::filesystem::exists(switchu::music::kSessionFlagPath, ec);
        const bool changed = session != m_lastSessionGuardState;
        if (changed) m_lastSessionGuardState = session;
        if (m_sessionGuardCb && (session || changed))
            m_sessionGuardCb(session);
    }
}



void MusicScreen::onUpdate(float dt) {
    m_uiTime += dt;
    m_statusTimer += dt;
    if (m_nextToastTimer > 0.f) m_nextToastTimer = std::max(0.f, m_nextToastTimer - dt);
    // The environment stays stable, while local Music surfaces can adopt the
    // selected album accent after its artwork style has been sampled.
    gMusicAccent = kMusicAccent;
    m_idleTime += std::max(0.f, dt);

    if (!m_active) {
        m_hiddenGuardTimer += dt;
        // FIX1: intentionally no cover-cache destruction while HOME is active.
        // GPU resource lifetime changes are deferred away from the Music->HOME
        // transition, which is the hardware crash path reported for V0.02.
        if (m_hiddenGuardTimer >= 0.20f) {
            m_hiddenGuardTimer = 0.f;
            refreshStatus(true);
            std::error_code ec;
            const bool active = std::filesystem::exists(switchu::music::kSessionFlagPath, ec);
            const bool changed = active != m_lastSessionGuardState;
            if (changed) m_lastSessionGuardState = active;
            // If daemon status IPC temporarily fails, the session flag still
            // reasserts Music ownership instead of allowing HOME BGM to leak in.
            if (m_sessionGuardCb && (active || changed))
                m_sessionGuardCb(active);
        }
        return;
    }

    m_hiddenGuardTimer = 0.f;
    m_transitionAlpha = std::min(1.f, m_transitionAlpha + dt / 0.22f);

    if (m_view == View::Albums || m_view == View::Playlists)
        m_rootInfoReveal = std::min(1.f, m_rootInfoReveal + std::max(0.f, dt) / 0.18f);
    if (m_closing) {
        // Exit stays intentionally direct: 110 ms visual fade, then the HOME
        // restore callback runs immediately with no extra wait stage.
        m_transitionAlpha = std::max(0.f, m_transitionAlpha - dt / timing::kExitToHomeSeconds);
        if (m_transitionAlpha <= 0.f) {
            m_closing = false;
            if (m_closeCb) m_closeCb(); else hide();
            return;
        }
    }

    // The root Music carousel runs through the exact motion engine used by
    // HOME IconGrid: same continuous coordinate, inertia, friction and snap.
    if (rootView()) {
        const auto motion = switchu::homeui::updateCarouselMotion(
            m_rootCarouselMotion, std::max(0.f, dt), rootItemCount());
        if (motion.settled) {
            const int settled = std::clamp(
                static_cast<int>(std::round(m_rootCarouselMotion.position)),
                0, std::max(0, rootItemCount() - 1));
            if (settled != m_selection) {
                m_selection = settled;
                m_rootInfoReveal = 0.f;
            }
        }
    }

    // Physical record phase is continuous. A track change adds a short boost,
    // then exponentially returns to the calm 0.74 rad/s presentation speed.
    const float safeDt = std::max(0.f, dt);
    m_vinylSpinPhase = std::fmod(
        m_vinylSpinPhase + safeDt * (0.74f + 4.10f * m_vinylSpinBoost),
        6.28318530718f);
    if (m_vinylSpinPhase < 0.f) m_vinylSpinPhase += 6.28318530718f;
    m_vinylSpinBoost *= std::exp(-safeDt * 3.15f);
    if (m_vinylSpinBoost < 0.002f) m_vinylSpinBoost = 0.f;

    // Three-layer micro-parallax: environment moves least, sleeve follows the
    // HOME carousel normally, and the record receives a few pixels of lag in
    // drawPhysicalMedia(). No camera motion is large enough to affect focus.
    float parallaxTargetX = 0.f;
    float parallaxTargetY = 0.f;
    if (rootView()) {
        const float motionSignal = std::clamp(
            m_rootCarouselMotion.velocity +
            (m_rootCarouselMotion.snapActive
                ? (m_rootCarouselMotion.snapTarget - m_rootCarouselMotion.position) * 4.2f
                : 0.f), -7.f, 7.f);
        parallaxTargetX = std::clamp(-motionSignal * 0.48f, -3.2f, 3.2f);
    } else if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail ||
               m_view == View::NowPlaying) {
        parallaxTargetX = std::sin(m_uiTime * 0.31f) * 1.25f;
        parallaxTargetY = std::cos(m_uiTime * 0.27f) * 0.70f;
    }
    const float parallaxBlend = 1.f - std::exp(-safeDt * 5.8f);
    m_sceneParallaxX += (parallaxTargetX - m_sceneParallaxX) * parallaxBlend;
    m_sceneParallaxY += (parallaxTargetY - m_sceneParallaxY) * parallaxBlend;

    if (m_rootEntryBounceActive) {
        m_rootEntryBounceTime += std::max(0.f, dt);
        if (m_rootEntryBounceTime >= switchu::homeui::kCarouselSelectionBounceDuration) {
            m_rootEntryBounceTime = switchu::homeui::kCarouselSelectionBounceDuration;
            m_rootEntryBounceActive = false;
        }
    }

    if (m_rootCategoryTransition < 1.f) {
        m_rootCategoryTransition = std::min(1.f,
            m_rootCategoryTransition + std::max(0.f, dt) / timing::kRootCategoryTransitionSeconds);
    }
    if (m_rootCategoryHintTimer > 0.f)
        m_rootCategoryHintTimer = std::max(0.f, m_rootCategoryHintTimer - std::max(0.f, dt));
    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail || m_view == View::Queue) {
        const float listBlend = 1.f - std::exp(-std::max(0.f, dt) * 18.0f);
        m_listVisualSelection +=
            (static_cast<float>(m_selection) - m_listVisualSelection) * listBlend;
    }

    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail) {
        const float speed = std::max(0.f, dt) / timing::kDetailTransformSeconds;
        if (m_detailClosing) {
            m_detailTransition = std::max(0.f, m_detailTransition - speed);
            if (m_detailTransition <= 0.f) {
                m_detailClosing = false;
                m_view = m_detailReturnView;
                m_selection = m_view == View::Albums
                    ? static_cast<int>(m_detailAlbum)
                    : static_cast<int>(m_detailPlaylist);
                switchu::homeui::jumpCarouselTo(m_rootCarouselMotion, m_selection, rootItemCount());
            }
        } else {
            m_detailTransition = std::min(1.f, m_detailTransition + speed);
        }
    }

    if (m_view == View::NowPlaying) {
        if (m_nowPlayingClosing) {
            m_nowPlayingEnter = std::max(0.f, m_nowPlayingEnter - std::max(0.f, dt) / timing::kNowPlayingExitSeconds);
            if (m_nowPlayingEnter <= 0.f) {
                m_nowPlayingClosing = false;
                m_view = m_nowPlayingReturnView;
                m_selection = m_nowPlayingReturnSelection;
                clampSelectionForView();
                if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail || m_view == View::Queue)
                    m_listVisualSelection = static_cast<float>(m_selection);
                m_nowPlayingEnter = 1.f;
            }
        } else {
            m_nowPlayingEnter = std::min(1.f, m_nowPlayingEnter + std::max(0.f, dt) / timing::kNowPlayingEnterSeconds);
        }
    }


    // Contextual marquee: only the currently selected track is allowed to
    // animate. Changing selection resets the title immediately to its start.
    uint64_t marqueeId = 0;
    if (m_view == View::AlbumDetail && m_detailAlbum < m_library.albums.size()) {
        const auto& tracks = m_library.albums[m_detailAlbum].tracks;
        if (!tracks.empty()) {
            const int idx = std::clamp(m_selection, 0, static_cast<int>(tracks.size()) - 1);
            const size_t ti = tracks[static_cast<size_t>(idx)];
            if (ti < m_library.tracks.size()) marqueeId = m_library.tracks[ti].id;
        }
    } else if (m_view == View::PlaylistDetail && m_detailPlaylist < m_playlistStore.playlists().size()) {
        const auto& ids = m_playlistStore.playlists()[m_detailPlaylist].trackIds;
        if (!ids.empty()) {
            const int idx = std::clamp(m_selection, 0, static_cast<int>(ids.size()) - 1);
            marqueeId = ids[static_cast<size_t>(idx)];
        }
    } else if (m_view == View::Queue) {
        const auto& ids = m_client.queueTrackIds();
        if (!ids.empty()) {
            const int idx = std::clamp(m_selection, 0, static_cast<int>(ids.size()) - 1);
            marqueeId = ids[static_cast<size_t>(idx)];
        }
    } else if (m_view == View::NowPlaying && m_status.track_id != 0) {
        marqueeId = m_status.track_id;
    }
    if (marqueeId != m_marqueeTrackId) {
        m_marqueeTrackId = marqueeId;
        m_marqueeElapsed = 0.f;
    } else if (marqueeId != 0) {
        m_marqueeElapsed += std::max(0.f, dt);
    } else {
        m_marqueeElapsed = 0.f;
    }

    finishScanIfReady();
    refreshStatus(false);

    // V8.3 crash fix: the console logs proved the library scan completes and
    // the crash occurs immediately after the old [music-style] worker begins.
    // Keep style resolution decoder-free. Normal cover textures are loaded
    // lazily on the render thread, while accent/spine/back colours come from
    // MusicCoverCache::styleFor() without any secondary image decode.
}


void MusicScreen::setRootCategory(int index) {
    m_idleTime = 0.f;
    if (!rootView() || m_closing || contentTransitionBusy()) return;
    const int previous = rootCategoryIndex();
    const int requested = index;
    index = (index % kRootCategoryCount + kRootCategoryCount) % kRootCategoryCount;
    if (previous == index &&
        ((index == 0 && m_view == View::Albums) || (index == 1 && m_view == View::Playlists)))
        return;

    // Preserve the physical D-pad direction even when the two-item category
    // list wraps (Albums Up -> Playlists, Playlists Down -> Albums).
    m_rootCategoryDirection = requested > previous ? 1 : -1;
    m_rootCategoryPreviousView = m_view;
    m_rootCategoryPreviousPosition = m_rootCarouselMotion.position;
    m_rootCategoryPreviousSelection = m_selection;
    m_rootCategoryTransition = 0.f;
    m_rootCategoryHintTimer = 0.85f;
    m_selection = 0;
    m_view = index == 0 ? View::Albums : View::Playlists;
    switchu::homeui::jumpCarouselTo(m_rootCarouselMotion, 0, rootItemCount());
    m_rootInfoReveal = 0.f;
}


bool MusicScreen::rootView() const {
    return m_view == View::Albums || m_view == View::Playlists;
}

int MusicScreen::rootCategoryIndex() const {
    return m_view == View::Playlists ? 1 : 0;
}

int MusicScreen::rootItemCount() const {
    if (m_view == View::Albums)
        return static_cast<int>(m_library.albums.size());
    if (m_view == View::Playlists)
        return static_cast<int>(m_playlistStore.playlists().size());
    return 0;
}

void MusicScreen::retargetRootCarousel(bool immediate) {
    if (!rootView()) return;
    const int count = rootItemCount();
    if (immediate)
        switchu::homeui::jumpCarouselTo(m_rootCarouselMotion, m_selection, count);
    else
        switchu::homeui::retargetCarouselSnap(m_rootCarouselMotion, m_selection, count);
}

bool MusicScreen::contentTransitionBusy() const {
    if (rootView() && m_rootCategoryTransition < 0.999f)
        return true;
    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail)
        return m_detailClosing || m_detailTransition < 0.999f;
    if (m_view == View::NowPlaying)
        return m_nowPlayingClosing || m_nowPlayingEnter < 0.999f;
    return false;
}

void MusicScreen::clampSelectionForView() {
    size_t count = 0;
    switch (m_view) {
        case View::Albums: count = m_library.albums.size(); break;
        case View::Playlists: count = m_playlistStore.playlists().size(); break;
        case View::AlbumDetail:
            if (m_detailAlbum < m_library.albums.size()) count = m_library.albums[m_detailAlbum].tracks.size();
            break;
        case View::PlaylistDetail:
            if (m_detailPlaylist < m_playlistStore.playlists().size()) count = m_playlistStore.playlists()[m_detailPlaylist].trackIds.size();
            break;
        case View::Queue: count = m_client.queueTrackIds().size(); break;
        case View::NowPlaying: return;
    }
    if (count == 0) m_selection = 0;
    else m_selection = std::clamp(m_selection, 0, static_cast<int>(count) - 1);
}

void MusicScreen::moveSelection(int dx, int dy) {
    if (dx != 0 || dy != 0) m_idleTime = 0.f;
    if (m_closing || contentTransitionBusy()) return;

    if (rootView()) {
        if (dx != 0) {
            const int before = m_selection;
            m_selection += dx;
            clampSelectionForView();
            if (m_selection != before) {
                DebugLog::log("[music-diag] MOVE_ROOT view=%d from=%d to=%d",
                              static_cast<int>(m_view), before, m_selection);
                m_rootInfoReveal = 0.f;
                retargetRootCarousel(false);
            }
        }
        return;
    }

    if (m_view == View::NowPlaying) {
        if (dy < 0) {
            m_nowControl = 5;
        } else if (dy > 0 && m_nowControl == 5) {
            m_nowControl = 2;
        } else if (dx != 0) {
            if (m_nowControl == 5) seekRelative(static_cast<int64_t>(dx) * 5'000);
            else updateNowPlayingSelection(dx);
        }
        return;
    }

    const int before = m_selection;
    m_selection += dy != 0 ? dy : dx;
    clampSelectionForView();
    if (m_selection != before)
        DebugLog::log("[music-diag] MOVE_LIST view=%d from=%d to=%d",
                      static_cast<int>(m_view), before, m_selection);
}

void MusicScreen::openAlbum(size_t index) {
    if (index >= m_library.albums.size()) return;
    DebugLog::log("[music-diag] OPEN_ALBUM index=%zu title=%s", index, m_library.albums[index].title.c_str());
    m_detailReturnView = View::Albums;
    m_detailAlbum = index;
    m_view = View::AlbumDetail;
    m_selection = 0;
    m_listVisualSelection = 0.f;
    m_detailTransition = 0.f;
    m_detailClosing = false;
}


void MusicScreen::openPlaylist(size_t index) {
    if (index >= m_playlistStore.playlists().size()) return;
    DebugLog::log("[music-diag] OPEN_PLAYLIST index=%zu name=%s", index, m_playlistStore.playlists()[index].name.c_str());
    m_detailReturnView = View::Playlists;
    m_detailPlaylist = index;
    m_view = View::PlaylistDetail;
    m_selection = 0;
    m_listVisualSelection = 0.f;
    m_detailTransition = 0.f;
    m_detailClosing = false;
}


std::vector<uint64_t> MusicScreen::albumTrackIds(size_t albumIndex) const {
    std::vector<uint64_t> ids;
    if (albumIndex >= m_library.albums.size()) return ids;
    for (size_t ti : m_library.albums[albumIndex].tracks)
        if (ti < m_library.tracks.size()) ids.push_back(m_library.tracks[ti].id);
    return ids;
}

std::vector<uint64_t> MusicScreen::playlistTrackIds(size_t playlistIndex) const {
    if (playlistIndex >= m_playlistStore.playlists().size()) return {};
    return m_playlistStore.playlists()[playlistIndex].trackIds;
}

void MusicScreen::playTrackIds(const std::vector<uint64_t>& ids, int index) {
    if (ids.empty()) return;
    index = std::clamp(index, 0, static_cast<int>(ids.size()) - 1);
    const float volume = std::clamp(m_status.volume, 0.f, 1.f);
    const bool shuffle = statusFlag(m_status, switchu::music::MusicStatus_Shuffle);
    const auto repeat = static_cast<switchu::music::RepeatMode>(m_status.repeat_mode);
    DebugLog::log("[music-diag] PLAY_TRACK queue=%zu index=%d id=%016llX",
                  ids.size(), index, static_cast<unsigned long long>(ids[static_cast<size_t>(index)]));
    if (m_client.writeQueueIds(m_library, ids, index, volume, shuffle, repeat) &&
        m_client.reloadQueue()) {
        m_client.loadQueueTrackIds();
        if (!m_client.playIndex(index)) {
            DebugLog::log("[music-diag] PLAY_TRACK command failed index=%d", index);
            return;
        }
        refreshStatus(true);
    } else {
        DebugLog::log("[music-diag] PLAY_TRACK queue reload failed");
    }
}


void MusicScreen::activateSelection() {
    m_idleTime = 0.f;
    if (m_scanRunning || m_closing || contentTransitionBusy()) return;
    switch (m_view) {
        case View::Albums:
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size())
                openAlbum(static_cast<size_t>(m_selection));
            break;
        case View::Playlists:
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists().size())
                openPlaylist(static_cast<size_t>(m_selection));
            break;
        case View::AlbumDetail:
            if (m_detailAlbum < m_library.albums.size()) playTrackIds(albumTrackIds(m_detailAlbum), m_selection);
            break;
        case View::PlaylistDetail:
            if (m_detailPlaylist < m_playlistStore.playlists().size()) playTrackIds(playlistTrackIds(m_detailPlaylist), m_selection);
            break;
        case View::Queue:
            if (!m_client.queueTrackIds().empty()) {
                DebugLog::log("[music-diag] QUEUE_PLAY index=%d", m_selection);
                if (m_client.playIndex(m_selection)) refreshStatus(true);
            }
            break;
        case View::NowPlaying:
            activateNowPlayingControl();
            break;
    }
}

void MusicScreen::goBack() {
    m_idleTime = 0.f;
    if (m_closing) return;
    switch (m_view) {
        case View::Albums:
        case View::Playlists:
            DebugLog::log("[music-diag] CLOSE_MUSIC requested view=%d", static_cast<int>(m_view));
            m_closing = true;
            setFocusable(false);
            return;
        case View::AlbumDetail:
        case View::PlaylistDetail:
            m_detailClosing = true;
            return;
        case View::NowPlaying:
            m_nowPlayingClosing = true;
            return;
        case View::Queue:
            m_view = m_queueReturnView;
            m_selection = m_queueReturnView == View::NowPlaying ? 0 : m_queueReturnSelection;
            if (m_view == View::NowPlaying) m_nowPlayingEnter = 1.f;
            clampSelectionForView();
            if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail || m_view == View::Queue)
                m_listVisualSelection = static_cast<float>(m_selection);
            return;
    }
}

size_t MusicScreen::selectedTrackIndex() const {
    if (m_view == View::AlbumDetail && m_detailAlbum < m_library.albums.size()) {
        const auto& tracks = m_library.albums[m_detailAlbum].tracks;
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < tracks.size())
            return tracks[static_cast<size_t>(m_selection)];
    }
    if (m_view == View::PlaylistDetail && m_detailPlaylist < m_playlistStore.playlists().size()) {
        const auto& ids = m_playlistStore.playlists()[m_detailPlaylist].trackIds;
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < ids.size()) {
            const auto found = m_library.trackById.find(ids[static_cast<size_t>(m_selection)]);
            if (found != m_library.trackById.end() && found->second < m_library.tracks.size())
                return found->second;
        }
    }
    return static_cast<size_t>(-1);
}

void MusicScreen::contextualX() {
    if (m_scanRunning || m_closing || contentTransitionBusy()) return;
    if (m_view == View::Albums) {
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size())
            playTrackIds(albumTrackIds(static_cast<size_t>(m_selection)), 0);
        return;
    }
    if (m_view == View::Playlists) {
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists().size())
            playTrackIds(playlistTrackIds(static_cast<size_t>(m_selection)), 0);
        return;
    }
    if (m_view == View::PlaylistDetail) {
        if (m_detailPlaylist < m_playlistStore.playlists().size() &&
            m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists()[m_detailPlaylist].trackIds.size()) {
            DebugLog::log("[music-diag] PLAYLIST_REMOVE playlist=%zu row=%d", m_detailPlaylist, m_selection);
            if (m_playlistStore.removeTrack(m_detailPlaylist, static_cast<size_t>(m_selection))) {
                m_playlistStore.save();
                clampSelectionForView();
            }
        }
        return;
    }
    const size_t ti = selectedTrackIndex();
    if (ti != static_cast<size_t>(-1) && ti < m_library.tracks.size())
        openPlaylistChooser(m_library.tracks[ti].id);
}

void MusicScreen::contextualY() {
    if (m_scanRunning || m_closing || contentTransitionBusy()) return;
    if (rootView()) return;
    if (m_view == View::AlbumDetail) {
        const size_t ti = selectedTrackIndex();
        if (ti != static_cast<size_t>(-1) && ti < m_library.tracks.size())
            openPlaylistChooser(m_library.tracks[ti].id);
        return;
    }
    if (m_view == View::PlaylistDetail) {
        appendToQueue(playlistTrackIds(m_detailPlaylist));
        return;
    }
    if (m_view == View::NowPlaying && !m_client.queueTrackIds().empty()) {
        m_queueReturnView = View::NowPlaying;
        m_queueReturnSelection = 0;
        m_view = View::Queue;
        m_selection = std::max(0, m_status.current_index);
        clampSelectionForView();
        m_listVisualSelection = static_cast<float>(m_selection);
    }
}

void MusicScreen::appendToQueue(const std::vector<uint64_t>& ids) {
    if (ids.empty()) return;
    std::vector<uint64_t> combined = m_client.queueTrackIds();
    const size_t previous = combined.size();
    combined.insert(combined.end(), ids.begin(), ids.end());

    const int current = m_status.current_index >= 0 ? m_status.current_index : 0;
    const float volume = std::clamp(m_status.volume, 0.f, 1.f);
    const bool shuffle = statusFlag(m_status, switchu::music::MusicStatus_Shuffle);
    const auto repeat = static_cast<switchu::music::RepeatMode>(m_status.repeat_mode);
    DebugLog::log("[music-diag] QUEUE_CHANGE append=%zu before=%zu after=%zu",
                  ids.size(), previous, combined.size());
    if (m_client.writeQueueIds(m_library, combined, current, volume, shuffle, repeat) &&
        m_client.reloadQueue()) {
        m_client.loadQueueTrackIds();
        refreshStatus(true);
    }
}

void MusicScreen::openCreatePlaylist() {
    m_modal = Modal::PlaylistNameKeyboard;
    m_keyboardText.clear();
    m_keyboardIndex = 0;
}

void MusicScreen::openPlaylistChooser(uint64_t trackId) {
    if (m_playlistStore.playlists().empty()) {
        m_pendingPlaylistTrackId = trackId;
        openCreatePlaylist();
        return;
    }
    m_pendingPlaylistTrackId = trackId;
    m_modal = Modal::PlaylistChooser;
    m_modalSelection = 0;
}

void MusicScreen::modalMove(int dx, int dy) {
    if (m_modal == Modal::PlaylistChooser) {
        if (!m_playlistStore.playlists().empty()) {
            m_modalSelection += (dy != 0 ? dy : dx);
            m_modalSelection = std::clamp(m_modalSelection, 0,
                static_cast<int>(m_playlistStore.playlists().size()) - 1);
        }
        return;
    }
    static constexpr int cols = 10;
    static constexpr int count = 42;
    int next = m_keyboardIndex + dx + dy * cols;
    m_keyboardIndex = std::clamp(next, 0, count - 1);
}

void MusicScreen::modalActivate() {
    if (m_modal == Modal::PlaylistChooser) {
        if (m_modalSelection >= 0 && static_cast<size_t>(m_modalSelection) < m_playlistStore.playlists().size()) {
            m_playlistStore.addTrack(static_cast<size_t>(m_modalSelection), m_pendingPlaylistTrackId);
            m_playlistStore.save();
        }
        m_pendingPlaylistTrackId = 0;
        m_modal = Modal::None;
        return;
    }

    static const std::array<std::string,42> keys = {
        "A","B","C","D","E","F","G","H","I","J",
        "K","L","M","N","O","P","Q","R","S","T",
        "U","V","W","X","Y","Z","0","1","2","3",
        "4","5","6","7","8","9"," ","-","_","'",".","←"
    };
    if (m_keyboardIndex < 0 || m_keyboardIndex >= static_cast<int>(keys.size())) return;
    const std::string& key = keys[static_cast<size_t>(m_keyboardIndex)];
    if (key == "←") { modalBackspace(); return; }
    if (m_keyboardText.size() < 48)
        m_keyboardText += key;
}

void MusicScreen::modalBackspace() {
    if (!m_keyboardText.empty()) m_keyboardText.pop_back();
}

void MusicScreen::modalConfirm() {
    if (m_modal != Modal::PlaylistNameKeyboard) return;
    std::string name = m_keyboardText;
    if (name.empty()) name = "Nouvelle playlist";
    const size_t created = m_playlistStore.create(name);
    if (m_pendingPlaylistTrackId != 0) m_playlistStore.addTrack(created, m_pendingPlaylistTrackId);
    m_pendingPlaylistTrackId = 0;
    m_playlistStore.save();
    m_view = View::Playlists;
    m_selection = static_cast<int>(created);
    switchu::homeui::jumpCarouselTo(m_rootCarouselMotion, m_selection, rootItemCount());
    m_rootInfoReveal = 0.f;
    m_modal = Modal::None;
}

void MusicScreen::modalCancel() {
    m_pendingPlaylistTrackId = 0;
    m_modal = Modal::None;
}

void MusicScreen::openNowPlaying() {
    if (!currentTrack() || m_closing || contentTransitionBusy())
        return;
    m_nowPlayingReturnView = m_view;
    m_nowPlayingReturnSelection = m_selection;
    m_view = View::NowPlaying;
    m_selection = 0;
    m_nowControl = 2;
    m_nowPlayingEnter = 0.f;
    m_nowPlayingClosing = false;
}

void MusicScreen::updateNowPlayingSelection(int delta) {
    m_nowControl = std::clamp(m_nowControl + delta, 0, 4);
}

void MusicScreen::activateNowPlayingControl() {
    switch (m_nowControl) {
        case 0: {
            const bool enabled = !statusFlag(m_status, switchu::music::MusicStatus_Shuffle);
            m_client.setShuffle(enabled);
            if (enabled) m_status.flags |= switchu::music::MusicStatus_Shuffle;
            else m_status.flags &= ~switchu::music::MusicStatus_Shuffle;
            break;
        }
        case 1: m_client.previous(); break;
        case 2: m_client.togglePause(); break;
        case 3: m_client.next(); break;
        case 4: {
            auto mode = static_cast<switchu::music::RepeatMode>(m_status.repeat_mode);
            if (mode == switchu::music::RepeatMode::Off) mode = switchu::music::RepeatMode::Queue;
            else if (mode == switchu::music::RepeatMode::Queue) mode = switchu::music::RepeatMode::Track;
            else mode = switchu::music::RepeatMode::Off;
            m_client.setRepeat(mode);
            m_status.repeat_mode = static_cast<uint8_t>(mode);
            break;
        }
    }
    refreshStatus(true);
}

void MusicScreen::seekRelative(int64_t deltaMs) {
    const int64_t pos = static_cast<int64_t>(m_status.position_ms) + deltaMs;
    const uint64_t clamped = static_cast<uint64_t>(std::max<int64_t>(0,
        std::min<int64_t>(static_cast<int64_t>(m_status.duration_ms), pos)));
    m_client.seekMs(clamped);
    m_status.position_ms = clamped;
}

const Track* MusicScreen::trackForId(uint64_t id) const {
    auto it = m_library.trackById.find(id);
    if (it == m_library.trackById.end() || it->second >= m_library.tracks.size()) return nullptr;
    return &m_library.tracks[it->second];
}

const Track* MusicScreen::currentTrack() const {
    return trackForId(m_status.track_id);
}

std::string MusicScreen::formatDuration(uint64_t ms) const {
    const uint64_t seconds = ms / 1000ULL;
    char out[32]{};
    if (seconds >= 3600)
        std::snprintf(out, sizeof(out), "%llu:%02llu:%02llu",
            static_cast<unsigned long long>(seconds / 3600),
            static_cast<unsigned long long>((seconds / 60) % 60),
            static_cast<unsigned long long>(seconds % 60));
    else
        std::snprintf(out, sizeof(out), "%llu:%02llu",
            static_cast<unsigned long long>(seconds / 60),
            static_cast<unsigned long long>(seconds % 60));
    return out;
}


std::string MusicScreen::fitText(nxui::Font* font, const std::string& text,
                                 float maxWidth, float scale) const {
    if (!font || text.empty() || maxWidth <= 0.f || scale <= 0.f)
        return text;
    if (font->measure(text).x * scale <= maxWidth)
        return text;

    // Truncate on UTF-8 codepoint boundaries without allocating a temporary
    // vector every frame. Long metadata is common in Music and this path can be
    // hit by several visible rows simultaneously.
    size_t codepoints=0;
    for (size_t i=0;i<text.size();) {
        const unsigned char c=static_cast<unsigned char>(text[i]);
        size_t len=1;
        if ((c&0xE0u)==0xC0u) len=2;
        else if ((c&0xF0u)==0xE0u) len=3;
        else if ((c&0xF8u)==0xF0u) len=4;
        i=std::min(text.size(),i+len);
        ++codepoints;
    }
    auto byteEndForCodepoints=[&text](size_t count) {
        size_t i=0;
        for (size_t n=0;n<count && i<text.size();++n) {
            const unsigned char c=static_cast<unsigned char>(text[i]);
            size_t len=1;
            if ((c&0xE0u)==0xC0u) len=2;
            else if ((c&0xF0u)==0xE0u) len=3;
            else if ((c&0xF8u)==0xF0u) len=4;
            i=std::min(text.size(),i+len);
        }
        return i;
    };

    static const std::string ellipsis="…";
    if (font->measure(ellipsis).x * scale > maxWidth) return {};

    size_t lo=0, hi=codepoints;
    while (lo<hi) {
        const size_t mid=(lo+hi+1)/2;
        const size_t end=byteEndForCodepoints(mid);
        const std::string candidate=text.substr(0,end)+ellipsis;
        if (font->measure(candidate).x*scale<=maxWidth) lo=mid;
        else hi=mid-1;
    }
    const size_t end=byteEndForCodepoints(lo);
    return text.substr(0,end)+ellipsis;
}

void MusicScreen::drawMarqueeOrFit(nxui::Renderer& ren, const std::string& text,
                                   const nxui::Rect& clip, float y, float scale,
                                   const nxui::Color& color, bool animate) const {
    if (!m_font || text.empty() || clip.width <= 0.f) return;
    const float fullWidth=m_font->measure(text).x*scale;
    if (fullWidth <= clip.width) {
        ren.drawText(text,{clip.x,y},m_font,color,scale);
        return;
    }
    if (!animate) {
        ren.drawText(fitText(m_font,text,clip.width,scale),{clip.x,y},m_font,color,scale);
        return;
    }

    const float travel=std::max(0.f,fullWidth-clip.width);
    constexpr float delay=1.0f;
    constexpr float speed=38.f;
    constexpr float endPause=0.60f;
    constexpr float returnDuration=0.34f;
    constexpr float startPause=0.56f;
    float offset=0.f;
    if (m_marqueeElapsed>delay && travel>0.5f) {
        const float forwardDuration=travel/speed;
        const float cycleDuration=forwardDuration+endPause+returnDuration+startPause;
        const float tCycle=std::fmod(m_marqueeElapsed-delay,std::max(0.01f,cycleDuration));
        if (tCycle<forwardDuration) offset=tCycle*speed;
        else if (tCycle<forwardDuration+endPause) offset=travel;
        else if (tCycle<forwardDuration+endPause+returnDuration) {
            const float u=(tCycle-forwardDuration-endPause)/returnDuration;
            offset=travel*(1.f-smoothStep(u));
        }
    }
    ren.pushClipRect(clip);
    ren.drawText(text,{clip.x-offset,y},m_font,color,scale);
    ren.popClipRect();
}

uint64_t MusicScreen::albumDurationMs(size_t albumIndex) const {
    if (albumIndex >= m_library.albums.size()) return 0;
    uint64_t total = 0;
    for (size_t ti : m_library.albums[albumIndex].tracks)
        if (ti < m_library.tracks.size()) total += m_library.tracks[ti].durationMs;
    return total;
}

uint64_t MusicScreen::playlistDurationMs(size_t playlistIndex) const {
    if (playlistIndex >= m_playlistStore.playlists().size()) return 0;
    uint64_t total = 0;
    for (uint64_t id : m_playlistStore.playlists()[playlistIndex].trackIds) {
        const Track* t = trackForId(id);
        if (t) total += t->durationMs;
    }
    return total;
}

CoverRef MusicScreen::playlistCover(size_t playlistIndex) const {
    if (playlistIndex >= m_playlistStore.playlists().size()) return {};
    for (uint64_t id : m_playlistStore.playlists()[playlistIndex].trackIds) {
        const Track* t = trackForId(id);
        if (t && t->cover.valid()) return t->cover;
    }
    return {};
}

nxui::Color MusicScreen::artworkAccent(const CoverRef& cover) const {
    const MusicArtworkStyle style = m_coverCache.styleFor(cover);
    return style.sampled ? style.accent : kMusicAccent;
}

PhysicalMediaGeometry MusicScreen::drawPhysicalMedia(nxui::Renderer& ren,
                                                       const CoverRef& cover,
                                                       const nxui::Rect& rect,
                                                       bool playlist,
                                                       float yawDeg,
                                                       float pitchDeg,
                                                       float rollDeg,
                                                       float zLiftPx,
                                                       float vinylReveal,
                                                       float vinylSpinRad,
                                                       float alphaValue,
                                                       bool playing,
                                                       int maxSide) {
    nxui::Texture* tex = m_coverCache.get(cover, ren, maxSide);
    // V8.3: no second artwork texture is decoded/generated for the rear sleeve.
    // The renderer receives nullptr and uses the safe smoked back colour.
    nxui::Texture* backTex = nullptr;
    const MusicArtworkStyle style = m_coverCache.styleFor(cover);
    PhysicalMediaPose pose{};
    pose.rect = rect;
    pose.accent = style.sampled ? style.accent : kMusicAccent;
    pose.spine = style.sampled ? style.spine : MusicArtworkStyle{}.spine;
    pose.back = style.sampled ? style.back : MusicArtworkStyle{}.back;
    pose.yawDeg = yawDeg;
    pose.pitchDeg = pitchDeg;
    pose.rollDeg = rollDeg;
    pose.depthPx = std::clamp(rect.width * 0.021f, 4.5f, 8.f);
    pose.zLiftPx = zLiftPx;
    pose.vinylReveal = vinylReveal;
    pose.vinylSpinRad = vinylSpinRad;
    pose.alpha = alphaValue * gMusicUiAlpha;
    pose.selected = vinylReveal > 0.45f;
    pose.playing = playing;
    pose.detailLevel = maxSide >= 500 ? 2 : (maxSide >= 280 ? 1 : 0);
    pose.vinylLagPx = -m_sceneParallaxX * (pose.selected ? 0.72f : 0.34f);

    const PhysicalMediaGeometry geometry = playlist
        ? drawPlaylistPhysicalMedia(ren, tex, backTex, pose)
        : drawAlbumPhysicalMedia(ren, tex, backTex, pose);

    const nxui::Rect floorClip{0.f, kFloorY, kScreenW, kFloorH};
    drawPhysicalMediaReflection(ren, tex, geometry, floorClip,
                                alphaValue * gMusicUiAlpha,
                                vinylReveal > 0.04f,
                                pose.accent);
    return geometry;
}

void MusicScreen::drawMusicBackground(nxui::Renderer& ren) {
    const float a = gMusicUiAlpha;

    // Stable premium dark environment. Album colours never repaint this layer.
    ren.drawGradientRect({0.f, 0.f, kScreenW, kScreenH},
                         {0.026f, 0.029f, 0.037f, a},
                         {0.009f, 0.011f, 0.016f, a});

    // Smoked Hi-Fi structure: almost invisible rack lines establish depth
    // without decorative circles or a constantly moving waveform.
    for (int y = 116; y < static_cast<int>(kFloorY); y += 42)
        ren.drawRect({0.f, static_cast<float>(y), kScreenW, 1.f},
                     {0.55f, 0.60f, 0.70f, 0.010f * a});
    const float backgroundParallaxX = m_sceneParallaxX * 0.28f;
    const float backgroundParallaxY = m_sceneParallaxY * 0.18f;
    for (int x = 96; x < 1280; x += 188)
        ren.drawRect({static_cast<float>(x) + backgroundParallaxX,
                      108.f + backgroundParallaxY, 1.f, kFloorY - 108.f},
                     {0.52f, 0.57f, 0.66f, 0.006f * a});

    // Diffuse studio light: a soft vertical ribbon, not a visible beam. It is
    // intentionally neutral so only the physical media picks up album colour.
    constexpr float lightCenter = 760.f;
    for (int i = -7; i <= 7; ++i) {
        const float t = std::abs(static_cast<float>(i)) / 7.f;
        const float w = 34.f;
        const float x = lightCenter + i * 30.f - w * 0.5f + backgroundParallaxX * 0.55f;
        ren.drawGradientRect({x, 82.f + backgroundParallaxY * 0.35f, w, kFloorY - 82.f},
                             {0.82f, 0.86f, 0.94f, (1.f - t) * 0.0065f * a},
                             {0.70f, 0.76f, 0.88f, (1.f - t) * 0.0015f * a});
    }

    // Reflective floor / Hi-Fi presentation surface.
    ren.drawGradientRect({0.f, kFloorY, kScreenW, kFloorH},
                         {0.074f, 0.080f, 0.092f, 0.34f * a},
                         {0.016f, 0.018f, 0.024f, 0.98f * a});
    ren.drawRect({0.f, kFloorY, kScreenW, 1.f},
                 {0.76f, 0.80f, 0.88f, 0.085f * a});
}

void MusicScreen::drawTopBar(nxui::Renderer& ren) {
    // Do not redraw a Music approximation of HOME's HUD. Render the actual
    // HOME widgets on top of Music's dark backdrop so dimensions, fonts,
    // Liquid Glass, category animation and shared HOME polish stays identical.
    // MusicIntegration sets the real DateTimeWidget to category 2 and hides
    // Wi-Fi on the real BatteryWidget while this screen is active.
    if (m_homeClockWidget) {
        m_homeClockWidget->setHomeTabsVisible(rootView());
        m_homeClockWidget->render(ren);
    }
    if (m_homeProfileWidget)
        m_homeProfileWidget->render(ren);
    if (m_homeBatteryWidget)
        m_homeBatteryWidget->render(ren);

    drawRootCategoryHint(ren);
}
void MusicScreen::drawRootCategoryHint(nxui::Renderer& ren) {
    if (!m_font || !rootView() || m_rootCategoryHintTimer <= 0.f)
        return;

    // Albums/Playlists is intentionally not a permanent category bar. A short
    // HOME-sized label confirms a D-pad Up/Down switch, then disappears.
    const float life = std::clamp(m_rootCategoryHintTimer / 0.85f, 0.f, 1.f);
    const float alpha = smoothStep(std::min(1.f, life * 1.8f));
    const std::string label = m_view == View::Albums ? "Albums" : "Playlists";
    const auto measured = m_font->measure(label);
    ren.drawText(label,
                 {640.f - measured.x * kHomeMinTextScale * 0.5f, 108.f},
                 m_font, textSecondary(0.72f * alpha), kHomeMinTextScale);
}


void MusicScreen::drawPreviousRootCarousel(nxui::Renderer& ren) {
    if (m_rootCategoryTransition >= 0.999f) return;
    const float e = smoothStep(m_rootCategoryTransition);
    const float oldAlpha = (1.f - e) * (1.f - 0.18f * e);
    if (oldAlpha <= 0.002f) return;

    const float saved = gMusicUiAlpha;
    const nxui::Color savedAccent = gMusicAccent;
    gMusicUiAlpha *= oldAlpha;
    const float yOffset = static_cast<float>(m_rootCategoryDirection) * e * 52.f;

    if (m_rootCategoryPreviousView == View::Albums) {
        const int count = static_cast<int>(m_library.albums.size());
        if (count > 0) {
            const int visualCenter = static_cast<int>(std::floor(m_rootCategoryPreviousPosition));
            for (int idx = std::max(0, visualCenter - 3);
                 idx <= std::min(count - 1, visualCenter + 4); ++idx) {
                const float delta = static_cast<float>(idx) - m_rootCategoryPreviousPosition;
                const float size = switchu::homeui::carouselIconSizeForDistance(delta);
                const float cx = 640.f + switchu::homeui::carouselCenterOffset(delta);
                const float y = kMusicCarouselBaselineY - size + yOffset;
                const bool selected = idx == m_rootCategoryPreviousSelection;
                drawPhysicalMedia(ren, m_library.albums[static_cast<size_t>(idx)].cover,
                                  {cx - size * .5f, y, size, size}, false,
                                  std::clamp(-delta * 4.6f, -7.f, 7.f), -0.6f, 0.f,
                                  selected ? 6.f * (1.f - e) : 0.f,
                                  selected ? 0.62f * (1.f - e) : 0.02f,
                                  0.f, 1.f, false,
                                  std::abs(delta) < .7f ? 512 :
                                  (std::abs(delta) < 1.65f ? 320 : 220));
            }
        }
    } else if (m_rootCategoryPreviousView == View::Playlists) {
        const auto& playlists = m_playlistStore.playlists();
        const int count = static_cast<int>(playlists.size());
        if (count > 0) {
            const int visualCenter = static_cast<int>(std::floor(m_rootCategoryPreviousPosition));
            for (int idx = std::max(0, visualCenter - 3);
                 idx <= std::min(count - 1, visualCenter + 4); ++idx) {
                const float delta = static_cast<float>(idx) - m_rootCategoryPreviousPosition;
                const float size = switchu::homeui::carouselIconSizeForDistance(delta);
                const float cx = 640.f + switchu::homeui::carouselCenterOffset(delta);
                const float y = kMusicCarouselBaselineY - size + yOffset;
                const bool selected = idx == m_rootCategoryPreviousSelection;
                drawPhysicalMedia(ren, playlistCover(static_cast<size_t>(idx)),
                                  {cx - size * .5f, y, size, size}, true,
                                  std::clamp(-delta * 4.2f, -7.f, 7.f), -0.6f, 0.f,
                                  selected ? 5.f * (1.f - e) : 0.f,
                                  selected ? 0.38f * (1.f - e) : 0.f,
                                  0.f, 1.f, false,
                                  std::abs(delta) < .7f ? 512 :
                                  (std::abs(delta) < 1.65f ? 320 : 220));
            }
        }
    }

    gMusicAccent = savedAccent;
    gMusicUiAlpha = saved;
}

void MusicScreen::drawEmpty(nxui::Renderer& ren, const std::string& title,
                            const std::string& detail) {
    if (!m_font) return;
    ren.drawText(fitText(m_font, title, 1120.f, 1.50f),
                 {78.f, 285.f}, m_font, textPrimary(), 1.50f);
    ren.drawText(fitText(m_font, detail, 1120.f, 0.94f),
                 {80.f, 340.f}, m_font, textSecondary(), 0.94f);
}

void MusicScreen::drawAlbums(nxui::Renderer& ren) {
    if (m_library.albums.empty()) {
        drawEmpty(ren, "Aucun album", "Ajoute des fichiers MP3 ou FLAC dans sdmc:/Music/.");
        return;
    }
    m_selection = std::clamp(m_selection, 0, static_cast<int>(m_library.albums.size()) - 1);
    drawPreviousRootCarousel(ren);

    const float cat = smoothStep(m_rootCategoryTransition);
    const float yOffset = -static_cast<float>(m_rootCategoryDirection) * (1.f - cat) * 52.f;
    const float savedAlpha = gMusicUiAlpha;
    gMusicUiAlpha *= 0.10f + 0.90f * cat;

    const float motionSignal = std::clamp(
        m_rootCarouselMotion.velocity +
        (m_rootCarouselMotion.snapActive
            ? (m_rootCarouselMotion.snapTarget - m_rootCarouselMotion.position) * 4.2f
            : 0.f), -7.f, 7.f);
    const bool idle = m_idleTime > 2.2f && !m_rootCarouselMotion.touchScrolling &&
                      !m_rootCarouselMotion.inertiaActive && !m_rootCarouselMotion.snapActive;

    auto albumIsPlaying = [this](size_t albumIndex) {
        if (!hasMusicSession() || albumIndex >= m_library.albums.size()) return false;
        for (size_t ti : m_library.albums[albumIndex].tracks)
            if (ti < m_library.tracks.size() && m_library.tracks[ti].id == m_status.track_id)
                return true;
        return false;
    };

    const int visualCenter = static_cast<int>(std::floor(m_rootCarouselMotion.position));
    for (int idx = std::max(0, visualCenter - 3);
         idx <= std::min(static_cast<int>(m_library.albums.size()) - 1, visualCenter + 4); ++idx) {
        const float delta = static_cast<float>(idx) - m_rootCarouselMotion.position;
        float size = switchu::homeui::carouselIconSizeForDistance(delta);
        if (m_rootEntryBounceActive && std::abs(delta) < 0.035f)
            size *= switchu::homeui::carouselSelectionBounceScale(m_rootEntryBounceTime);

        const float focus = smoothStep(clamp01(1.f - std::abs(delta) / 0.72f));
        const bool playing = albumIsPlaying(static_cast<size_t>(idx));
        const float idleLift = idle ? std::sin(m_uiTime * 0.78f) * 2.2f * focus : 0.f;
        const float idleYaw = idle ? std::sin(m_uiTime * 0.46f) * 0.50f * focus : 0.f;
        const float baseYaw = std::clamp(-delta * 4.6f, -7.5f, 7.5f);
        const float inertiaYaw = std::clamp(-motionSignal * 0.72f, -4.2f, 4.2f);
        const float yaw = baseYaw + inertiaYaw + idleYaw;
        const float roll = std::clamp(-motionSignal * 0.16f, -1.1f, 1.1f);
        // Reference HOME-Music carousel: sleeves stay clean; vinyl appears only
        // after opening an album or in Now Playing.
        const float reveal = 0.f;
        const float zLift = 10.f * focus;

        const float cx = 640.f + switchu::homeui::carouselCenterOffset(delta) +
                         m_sceneParallaxX * 0.58f * focus;
        const float y = kMusicCarouselBaselineY - size + yOffset + idleLift +
                        m_sceneParallaxY * 0.42f * focus;
        const nxui::Rect cardRect{cx - size * 0.5f, y, size, size};
        if (cardRect.x + cardRect.width <= -42.f || cardRect.x >= kScreenW + 42.f)
            continue;
        drawPhysicalMedia(ren, m_library.albums[static_cast<size_t>(idx)].cover,
                          cardRect, false, yaw, -0.7f, roll, zLift, reveal,
                          playing ? m_vinylSpinPhase : 0.f,
                          1.f, playing,
                          std::abs(delta) < 0.70f ? 512 :
                          (std::abs(delta) < 1.65f ? 320 : 220));
    }

    const float infoAlpha = smoothStep(m_rootInfoReveal) * cat;
    const auto& album = m_library.albums[static_cast<size_t>(m_selection)];
    gMusicAccent = artworkAccent(album.cover);
    if (m_homeTitlePill) {
        m_homeTitlePill->setText(album.title.empty() ? "Album sans titre" : album.title, kScreenW);
        m_homeTitlePill->setMusicDurationText(
            "♪  " + std::to_string(album.tracks.size()) + " titres  •  ◷  " +
            formatDuration(albumDurationMs(static_cast<size_t>(m_selection))));
        m_homeTitlePill->setMusicPlayable(!album.tracks.empty());
        m_homeTitlePill->setOpacity(savedAlpha * infoAlpha);
        m_homeTitlePill->setVisible(true);
        m_homeTitlePill->render(ren);
        m_homeTitlePill->setOpacity(1.f);

        const std::string artist = fitText(
            m_font,
            album.artist.empty() ? "Artiste inconnu" : album.artist,
            620.f, 0.92f);
        const auto artistSize = m_font->measure(artist);
        ren.drawText(artist,
                     {640.f - artistSize.x * 0.92f * 0.5f, 572.f},
                     m_font, textSecondary(0.90f * infoAlpha), 0.92f);
    }

    gMusicUiAlpha = savedAlpha;
}

float MusicScreen::visibleListStartVisual(size_t count, int rows) const {
    if (count <= static_cast<size_t>(rows)) return 0.f;
    const float maxStart = static_cast<float>(count - static_cast<size_t>(rows));
    return std::clamp(m_listVisualSelection - static_cast<float>(rows / 2), 0.f, maxStart);
}

void MusicScreen::drawNowPlaying(nxui::Renderer& ren) {
    const Track* track = currentTrack();
    if (!track) {
        drawEmpty(ren, "Aucune lecture en cours", "Choisis un morceau dans ta bibliothèque.");
        return;
    }

    const float e = std::clamp(m_nowPlayingEnter, 0.f, 1.f);
    const float saved = gMusicUiAlpha;
    gMusicUiAlpha *= e;
    const float shift = (1.f - e) * 32.f;
    const bool playing = statusFlag(m_status, switchu::music::MusicStatus_Playing);

    // Reference layout: large sleeve on the left, vinyl clearly visible behind
    // it, no information glass panel and no "En lecture" pill.
    drawPhysicalMedia(ren, track->cover,
                      {50.f + m_sceneParallaxX * 0.66f,
                       140.f + shift + m_sceneParallaxY * 0.54f,
                       394.f, 394.f}, false,
                      -1.1f + std::sin(m_uiTime * 0.28f) * 0.24f,
                      -0.8f, 0.f, 8.f,
                      0.98f, playing ? m_vinylSpinPhase : 0.f,
                      1.f, playing, 512);
    gMusicAccent = artworkAccent(track->cover);

    const std::string nowTitle = track->title.empty() ? "Morceau sans titre" : track->title;
    const std::string nowArtist = track->artist.empty() ? "Artiste inconnu" : track->artist;
    const float titleScale = 1.52f;
    const float artistScale = 0.96f;
    const float titleY = 214.f + shift;
    const float artistY = 270.f + shift;

    const float nowTitleW = m_font->measure(nowTitle).x * titleScale;
    const float nowArtistW = m_font->measure(nowArtist).x * artistScale;
    const bool titleMarquee = nowTitleW > 470.f;
    const bool artistMarquee = !titleMarquee && nowArtistW > 470.f;
    drawMarqueeOrFit(ren, nowTitle, {700.f, titleY - 5.f, 472.f, 48.f},
                     titleY, titleScale, textPrimary(), titleMarquee);
    drawMarqueeOrFit(ren, nowArtist, {701.f, artistY - 4.f, 470.f, 36.f},
                     artistY, artistScale, textSecondary(0.88f), artistMarquee);

    // Decorative heart from the reference. It does not masquerade as a
    // focusable control because V8.3 has no favourites backend.
    ren.drawText("♡", {1180.f, 214.f + shift}, m_font, textPrimary(0.94f), 1.45f);

    const float progress = m_status.duration_ms > 0
        ? std::clamp(float(double(m_status.position_ms) / double(m_status.duration_ms)), 0.f, 1.f)
        : 0.f;
    const nxui::Rect timeline{762.f, 367.f + shift, 397.f, 5.f};
    if (m_nowControl == 5)
        ren.drawRoundedRectOutline(timeline.expanded(8.f), accent(0.52f), 10.f, 1.2f);
    ren.drawRoundedRect(timeline, {0.24f, 0.25f, 0.28f, 0.86f * gMusicUiAlpha}, 3.f);
    ren.drawRoundedRect({timeline.x, timeline.y, timeline.width * progress, timeline.height},
                        textPrimary(), 3.f);
    const float knobX = timeline.x + timeline.width * progress;
    ren.drawCircle({knobX, timeline.y + timeline.height * 0.5f}, 7.f, textPrimary(), 24);

    const std::string elapsed = formatDuration(m_status.position_ms);
    const std::string duration = formatDuration(m_status.duration_ms);
    const auto durSz = m_font->measure(duration);
    ren.drawText(elapsed, {700.f, 354.f + shift}, m_font,
                 textSecondary(0.92f), kHomeMinTextScale);
    ren.drawText(duration,
                 {1214.f - durSz.x * kHomeMinTextScale, 354.f + shift},
                 m_font, textSecondary(0.92f), kHomeMinTextScale);

    // Transport row. The centre play/pause control is intentionally larger,
    // while the four side actions carry the labels visible in the reference.
    const std::array<float, 5> centers = {735.f, 840.f, 945.f, 1050.f, 1165.f};
    const float cy = 457.f + shift;
    for (int i = 0; i < 5; ++i) {
        const bool focused = i == m_nowControl;
        const float cx = centers[static_cast<size_t>(i)];

        if (i == 2) {
            const nxui::Color ring = focused ? accent(0.95f) : textPrimary(0.92f);
            ren.drawCircle({cx, cy}, 44.f, ring, 48);
            ren.drawCircle({cx, cy}, focused ? 41.5f : 42.3f,
                           {0.020f, 0.022f, 0.027f, 0.98f * gMusicUiAlpha}, 48);
            if (playing) {
                ren.drawRoundedRect({cx - 12.f, cy - 18.f, 7.f, 36.f}, textPrimary(), 2.f);
                ren.drawRoundedRect({cx + 5.f, cy - 18.f, 7.f, 36.f}, textPrimary(), 2.f);
            } else {
                ren.drawTriangle({cx - 10.f, cy - 20.f},
                                 {cx - 10.f, cy + 20.f},
                                 {cx + 23.f, cy}, textPrimary());
            }
            continue;
        }

        if (focused)
            ren.drawRoundedRect({cx - 30.f, cy - 29.f, 60.f, 58.f}, accent(0.10f), 20.f);

        const nxui::Color iconColor = focused ? accent(0.98f) : textPrimary(0.94f);
        if (i == 0) {
            ren.drawText("⇄", {cx - 16.f, cy - 19.f}, m_font, iconColor, 1.08f);
        } else if (i == 1) {
            ren.drawRect({cx - 16.f, cy - 15.f, 3.f, 30.f}, iconColor);
            ren.drawTriangle({cx + 12.f, cy - 17.f}, {cx + 12.f, cy + 17.f},
                             {cx - 12.f, cy}, iconColor);
        } else if (i == 3) {
            ren.drawRect({cx + 13.f, cy - 15.f, 3.f, 30.f}, iconColor);
            ren.drawTriangle({cx - 12.f, cy - 17.f}, {cx - 12.f, cy + 17.f},
                             {cx + 12.f, cy}, iconColor);
        } else if (i == 4) {
            ren.drawText(repeatLabel(m_status.repeat_mode),
                         {cx - 14.f, cy - 18.f}, m_font, iconColor, 1.08f);
        }
    }

    const std::array<std::string, 4> labels = {
        "Aléatoire", "Précédent", "Suivant", "Répéter"
    };
    const std::array<float, 4> labelCenters = {735.f, 840.f, 1050.f, 1165.f};
    for (size_t i = 0; i < labels.size(); ++i) {
        const auto size = m_font->measure(labels[i]);
        ren.drawText(labels[i],
                     {labelCenters[i] - size.x * kHomeMinTextScale * 0.5f,
                      494.f + shift},
                     m_font, textSecondary(0.92f), kHomeMinTextScale);
    }

    gMusicUiAlpha = saved;
}

void MusicScreen::drawNowPlayingIndicator(nxui::Renderer& ren, const nxui::Rect& row) {
    // Four-bar equalizer, intentionally separate from the selection state.
    // It marks the track that is actually playing even when the cursor is on
    // another row.
    const float phase = m_uiTime * 8.4f;
    const float x = row.x + row.width - 104.f;
    const float baseY = row.y + row.height * 0.5f + 9.f;
    for (int i = 0; i < 4; ++i) {
        const float wave = 0.5f + 0.5f * std::sin(phase + static_cast<float>(i) * 1.37f);
        const float h = 7.f + wave * (9.f + static_cast<float>((i + 1) % 3) * 2.f);
        ren.drawRoundedRect({x + i * 7.f, baseY - h, 4.f, h}, accent(0.98f), 2.f);
    }
}

void MusicScreen::drawTrackRow(nxui::Renderer& ren, const Track& t, const nxui::Rect& row,
                               bool selected, int ordinal) {
    if (selected) {
        // The selected row is tinted from the REAL album accent sampled during
        // the first cover decode. Keep it dark/translucent so text remains the
        // dominant information, matching the supplied reference.
        const nxui::Color fill{
            0.018f + gMusicAccent.r * 0.19f,
            0.020f + gMusicAccent.g * 0.19f,
            0.024f + gMusicAccent.b * 0.19f,
            0.74f * gMusicUiAlpha
        };
        ren.drawRoundedRect(row, fill, 10.f);
        ren.drawRoundedRectOutline(row, accent(0.72f), 10.f, 1.15f);
        ren.drawRoundedRectOutline(row.shrunk(1.5f), accent(0.13f), 8.5f, 1.f);
    }

    if (ordinal > 0) {
        char num[12]{};
        std::snprintf(num, sizeof(num), "%d", ordinal);
        ren.drawText(num, {row.x + 17.f, row.y + 15.f}, m_font,
                     selected ? accent(0.98f) : textSecondary(0.86f),
                     kHomeMinTextScale);
    }

    const bool playing = hasMusicSession() && m_status.track_id == t.id;
    if (playing) drawNowPlayingIndicator(ren, row);

    const float titleScale = 0.90f;
    const float titleX = row.x + 58.f;
    const std::string duration = formatDuration(t.durationMs);
    const float durationW = m_font ? m_font->measure(duration).x * kHomeMinTextScale : 56.f;
    const float durationX = row.x + row.width - durationW - 16.f;
    const float activeReserve = playing ? 54.f : 12.f;
    const float maxTitleW = std::max(40.f, durationX - titleX - activeReserve);
    const std::string fullTitle = t.title.empty() ? "Morceau sans titre" : t.title;
    const float fullWidth = m_font ? m_font->measure(fullTitle).x * titleScale : 0.f;

    const bool animateMarquee = selected && m_marqueeTrackId == t.id && fullWidth > maxTitleW;
    drawMarqueeOrFit(ren, fullTitle,
                     {titleX, row.y + 4.f, maxTitleW, row.height - 8.f},
                     row.y + 12.f, titleScale,
                     selected ? textPrimary() : textPrimary(0.94f), animateMarquee);

    ren.drawText(duration, {durationX, row.y + 14.f}, m_font,
                 selected ? accent(0.94f) : textSecondary(0.82f),
                 kHomeMinTextScale);
}

void MusicScreen::drawPlaylists(nxui::Renderer& ren) {
    const auto& playlists = m_playlistStore.playlists();
    if (playlists.empty()) {
        drawEmpty(ren, "Aucune playlist", "Appuie sur + pour créer ta première playlist locale.");
        return;
    }
    m_selection = std::clamp(m_selection, 0, static_cast<int>(playlists.size()) - 1);
    drawPreviousRootCarousel(ren);

    const float cat = smoothStep(m_rootCategoryTransition);
    const float yOffset = -static_cast<float>(m_rootCategoryDirection) * (1.f - cat) * 52.f;
    const float savedAlpha = gMusicUiAlpha;
    gMusicUiAlpha *= 0.10f + 0.90f * cat;

    const float motionSignal = std::clamp(
        m_rootCarouselMotion.velocity +
        (m_rootCarouselMotion.snapActive
            ? (m_rootCarouselMotion.snapTarget - m_rootCarouselMotion.position) * 4.2f
            : 0.f), -7.f, 7.f);
    const bool idle = m_idleTime > 2.2f && !m_rootCarouselMotion.touchScrolling &&
                      !m_rootCarouselMotion.inertiaActive && !m_rootCarouselMotion.snapActive;

    const int visualCenter = static_cast<int>(std::floor(m_rootCarouselMotion.position));
    for (int idx = std::max(0, visualCenter - 3);
         idx <= std::min(static_cast<int>(playlists.size()) - 1, visualCenter + 4); ++idx) {
        const float delta = static_cast<float>(idx) - m_rootCarouselMotion.position;
        float size = switchu::homeui::carouselIconSizeForDistance(delta);
        if (m_rootEntryBounceActive && std::abs(delta) < 0.035f)
            size *= switchu::homeui::carouselSelectionBounceScale(m_rootEntryBounceTime);

        const float focus = smoothStep(clamp01(1.f - std::abs(delta) / 0.72f));
        const float idleLift = idle ? std::sin(m_uiTime * 0.74f) * 1.8f * focus : 0.f;
        const float idleYaw = idle ? std::sin(m_uiTime * 0.43f) * 0.42f * focus : 0.f;
        const float yaw = std::clamp(-delta * 4.2f - motionSignal * 0.68f + idleYaw, -8.5f, 8.5f);
        const float roll = std::clamp(-motionSignal * 0.14f, -1.f, 1.f);
        const float reveal = 0.f;
        const float zLift = 8.f * focus;
        const float cx = 640.f + switchu::homeui::carouselCenterOffset(delta) +
                         m_sceneParallaxX * 0.52f * focus;
        const float y = kMusicCarouselBaselineY - size + yOffset + idleLift +
                        m_sceneParallaxY * 0.38f * focus;
        const nxui::Rect cardRect{cx - size * 0.5f, y, size, size};
        if (cardRect.x + cardRect.width <= -42.f || cardRect.x >= kScreenW + 42.f)
            continue;
        drawPhysicalMedia(ren, playlistCover(static_cast<size_t>(idx)),
                          cardRect, true, yaw, -0.6f, roll, zLift, reveal,
                          0.f, 1.f, false,
                          std::abs(delta) < 0.70f ? 512 :
                          (std::abs(delta) < 1.65f ? 320 : 220));
    }

    const float infoAlpha = smoothStep(m_rootInfoReveal) * cat;
    const auto& playlist = playlists[static_cast<size_t>(m_selection)];
    gMusicAccent = artworkAccent(playlistCover(static_cast<size_t>(m_selection)));
    if (m_homeTitlePill) {
        m_homeTitlePill->setText(playlist.name.empty() ? "Playlist" : playlist.name, kScreenW);
        m_homeTitlePill->setMusicDurationText("◷  " +
            formatDuration(playlistDurationMs(static_cast<size_t>(m_selection))));
        m_homeTitlePill->setMusicPlayable(!playlist.trackIds.empty());
        m_homeTitlePill->setOpacity(savedAlpha * infoAlpha);
        m_homeTitlePill->setVisible(true);
        m_homeTitlePill->render(ren);
        m_homeTitlePill->setOpacity(1.f);
    }

    gMusicUiAlpha = savedAlpha;
}

void MusicScreen::drawAlbumDetail(nxui::Renderer& ren) {
    if (m_detailAlbum >= m_library.albums.size()) {
        drawEmpty(ren, "Album indisponible", "La bibliothèque a changé. Reviens au carrousel.");
        return;
    }

    const auto& album = m_library.albums[m_detailAlbum];
    const float e = smoothStep(clamp01(m_detailTransition));
    const float rootFade = (1.f - e) * (1.f - e);

    auto albumIsPlaying = [this](size_t albumIndex) {
        if (!hasMusicSession() || albumIndex >= m_library.albums.size()) return false;
        for (size_t ti : m_library.albums[albumIndex].tracks)
            if (ti < m_library.tracks.size() && m_library.tracks[ti].id == m_status.track_id)
                return true;
        return false;
    };

    // Keep the HOME-carousel continuity for the neighbours, but remove the
    // HOME title pill immediately: the Album screen deliberately has no top
    // navigation pill and uses that space for album information.
    if (rootFade > 0.002f) {
        const float saved = gMusicUiAlpha;
        for (int idx = std::max(0, static_cast<int>(m_detailAlbum) - 2);
             idx <= std::min(static_cast<int>(m_library.albums.size()) - 1,
                             static_cast<int>(m_detailAlbum) + 2); ++idx) {
            if (idx == static_cast<int>(m_detailAlbum)) continue;
            const float delta = static_cast<float>(idx) - static_cast<float>(m_detailAlbum);
            const float size = switchu::homeui::carouselIconSizeForDistance(delta);
            float cx = 640.f + switchu::homeui::carouselCenterOffset(delta);
            cx += (delta < 0.f ? -1.f : 1.f) * 92.f * e;
            const float y = kMusicCarouselBaselineY - size + 8.f * e;
            gMusicUiAlpha = saved * rootFade * (std::abs(delta) > 1.f ? 0.50f : 0.78f);
            drawPhysicalMedia(ren, m_library.albums[static_cast<size_t>(idx)].cover,
                              {cx - size * 0.5f, y, size, size}, false,
                              std::clamp(-delta * 4.6f, -7.f, 7.f), -0.6f, 0.f,
                              0.f, 0.f, 0.f, 1.f,
                              albumIsPlaying(static_cast<size_t>(idx)), 320);
        }
        gMusicUiAlpha = saved;
    }

    constexpr float homeSelected = switchu::homeui::kCarouselSelectedSize;
    const nxui::Rect start{640.f - homeSelected * 0.5f,
                           kMusicCarouselBaselineY - homeSelected,
                           homeSelected, homeSelected};
    const nxui::Rect end{42.f + m_sceneParallaxX * 0.58f * e,
                         142.f + m_sceneParallaxY * 0.48f * e,
                         384.f, 384.f};
    const nxui::Rect cover{
        start.x + (end.x - start.x) * e,
        start.y + (end.y - start.y) * e,
        start.width + (end.width - start.width) * e,
        start.height + (end.height - start.height) * e
    };

    const float cinematic = std::sin(clamp01(e) * 3.14159265f);
    const bool playingAlbum = albumIsPlaying(m_detailAlbum);
    const float vinylReveal = 0.66f + 0.30f * e + 0.04f * cinematic;
    drawPhysicalMedia(ren, album.cover, cover, false,
                      -0.8f + 3.6f * cinematic,
                      -0.7f - 0.35f * cinematic,
                      0.8f * cinematic,
                      8.f + 6.f * cinematic,
                      vinylReveal,
                      playingAlbum ? m_vinylSpinPhase : cinematic * 0.34f,
                      1.f, playingAlbum, 512);

    // drawPhysicalMedia() performs the only cover decode. styleFor() now
    // returns the accent sampled from that same RGBA buffer.
    gMusicAccent = artworkAccent(album.cover);

    const float saved = gMusicUiAlpha;
    const float reveal = smoothStep((e - 0.14f) / 0.86f);
    gMusicUiAlpha *= reveal;
    const float listShift = (1.f - reveal) * 62.f;

    const float rightX = 732.f + listShift;
    ren.drawText(fitText(m_font,
                         album.title.empty() ? "Album sans titre" : album.title,
                         500.f, 1.30f),
                 {rightX, 84.f}, m_font, textPrimary(), 1.30f);
    ren.drawText(fitText(m_font,
                         album.artist.empty() ? "Artiste inconnu" : album.artist,
                         500.f, 0.92f),
                 {rightX + 1.f, 126.f}, m_font, textSecondary(0.90f), 0.92f);

    // Reference track window: one restrained dark panel with seven large rows.
    const nxui::Rect panel{732.f + listShift, 164.f, 508.f, 446.f};
    ren.drawRoundedRect(panel, {0.020f, 0.022f, 0.027f, 0.78f * gMusicUiAlpha}, 12.f);
    ren.drawRoundedRectOutline(panel, {0.92f, 0.94f, 0.98f, 0.18f * gMusicUiAlpha},
                               12.f, 1.f);

    constexpr int rows = 7;
    constexpr float rowPitch = 61.f;
    constexpr float rowHeight = 56.f;
    const float listX = panel.x + 8.f;
    const float listY = panel.y + 9.f;
    const float rowW = panel.width - 16.f;

    if (album.tracks.empty()) {
        ren.drawText("Aucun morceau", {listX + 18.f, listY + 32.f},
                     m_font, textSecondary(), 0.94f);
    } else {
        const float startRow = visibleListStartVisual(album.tracks.size(), rows);
        const int first = std::max(0, static_cast<int>(std::floor(startRow)));
        const int last = std::min(static_cast<int>(album.tracks.size()) - 1,
                                  static_cast<int>(std::ceil(startRow + rows)));
        ren.pushClipRect({listX, listY, rowW, rows * rowPitch - 3.f});
        for (int idx = first; idx <= last; ++idx) {
            const size_t ti = album.tracks[static_cast<size_t>(idx)];
            if (ti >= m_library.tracks.size()) continue;
            const Track& track = m_library.tracks[ti];
            const int ordinal = track.trackNumber > 0 ? track.trackNumber : idx + 1;
            const float rowY = listY + (static_cast<float>(idx) - startRow) * rowPitch;
            const nxui::Rect row{listX, rowY, rowW, rowHeight};
            drawTrackRow(ren, track, row, idx == m_selection, ordinal);
            if (idx != m_selection && idx + 1 < static_cast<int>(album.tracks.size())) {
                ren.drawRect({row.x + 16.f, row.y + row.height + 2.f,
                              row.width - 32.f, 1.f},
                             {0.82f, 0.85f, 0.90f, 0.10f * gMusicUiAlpha});
            }
        }
        ren.popClipRect();

        if (album.tracks.size() > static_cast<size_t>(rows)) {
            constexpr float railH = 410.f;
            const float thumbH = std::max(44.f,
                railH * rows / static_cast<float>(album.tracks.size()));
            const float ratio = startRow /
                static_cast<float>(album.tracks.size() - rows);
            const float railX = panel.x + panel.width - 5.f;
            const float railY = panel.y + 18.f;
            ren.drawRoundedRect({railX, railY, 2.f, railH}, textSecondary(0.10f), 1.f);
            ren.drawRoundedRect({railX, railY + (railH - thumbH) * ratio, 2.f, thumbH},
                                accent(0.58f), 1.f);
        }
    }

    gMusicUiAlpha = saved;
}

void MusicScreen::drawPlaylistDetail(nxui::Renderer& ren) {
    if (m_detailPlaylist >= m_playlistStore.playlists().size()) {
        drawEmpty(ren, "Playlist indisponible", "La bibliothèque a changé. Reviens au carrousel.");
        return;
    }
    const auto& playlist = m_playlistStore.playlists()[m_detailPlaylist];
    const CoverRef coverRef = playlistCover(m_detailPlaylist);
    const float e = smoothStep(clamp01(m_detailTransition));
    const float rootFade = (1.f - e) * (1.f - e);

    if (rootFade > 0.002f) {
        const auto& playlists = m_playlistStore.playlists();
        const float saved = gMusicUiAlpha;
        for (int idx = std::max(0, static_cast<int>(m_detailPlaylist) - 2);
             idx <= std::min(static_cast<int>(playlists.size()) - 1,
                             static_cast<int>(m_detailPlaylist) + 2); ++idx) {
            if (idx == static_cast<int>(m_detailPlaylist)) continue;
            const float delta = static_cast<float>(idx) - static_cast<float>(m_detailPlaylist);
            const float size = switchu::homeui::carouselIconSizeForDistance(delta);
            float cx = 640.f + switchu::homeui::carouselCenterOffset(delta);
            cx += (delta < 0.f ? -1.f : 1.f) * 74.f * e;
            const float y = kMusicCarouselBaselineY - size + 10.f * e;
            gMusicUiAlpha = saved * rootFade * (std::abs(delta) > 1.f ? 0.58f : 0.86f);
            drawPhysicalMedia(ren, playlistCover(static_cast<size_t>(idx)),
                              {cx - size * 0.5f, y, size, size}, true,
                              std::clamp(-delta * 4.2f, -7.f, 7.f), -0.6f, 0.f,
                              0.f, 0.f, 0.f, 1.f, false, 320);
        }

        if (m_homeTitlePill) {
            m_homeTitlePill->setText(playlist.name.empty() ? "Playlist" : playlist.name, kScreenW);
            m_homeTitlePill->setMusicDurationText("◷  " + formatDuration(playlistDurationMs(m_detailPlaylist)));
            m_homeTitlePill->setMusicPlayable(!playlist.trackIds.empty());
            m_homeTitlePill->setOpacity(saved * rootFade);
            m_homeTitlePill->setVisible(true);
            m_homeTitlePill->render(ren);
            m_homeTitlePill->setOpacity(1.f);
        }
        gMusicUiAlpha = saved;
    }

    constexpr float homeSelected = switchu::homeui::kCarouselSelectedSize;
    const nxui::Rect start{640.f - homeSelected * 0.5f,
                           kMusicCarouselBaselineY - homeSelected,
                           homeSelected, homeSelected};
    const nxui::Rect end{72.f + m_sceneParallaxX * 0.62f * e,
                           154.f + m_sceneParallaxY * 0.50f * e, 354.f, 354.f};
    const nxui::Rect cover{
        start.x + (end.x - start.x) * e,
        start.y + (end.y - start.y) * e,
        start.width + (end.width - start.width) * e,
        start.height + (end.height - start.height) * e
    };
    const float cinematic = std::sin(clamp01(e) * 3.14159265f);
    drawPhysicalMedia(ren, coverRef, cover, true,
                      -1.0f + 4.4f * cinematic,
                      -0.7f, 0.9f * cinematic,
                      8.f + 6.f * cinematic,
                      0.38f + 0.10f * e,
                      0.f, 1.f, false, 512);
    gMusicAccent = artworkAccent(coverRef);

    const float saved = gMusicUiAlpha;
    const float reveal = smoothStep((e - 0.16f) / 0.84f);
    gMusicUiAlpha *= reveal;
    const std::string info = formatDuration(playlistDurationMs(m_detailPlaylist)) + "  •  " +
                             std::to_string(playlist.trackIds.size()) + " morceaux";
    ren.drawText(fitText(m_font, playlist.name.empty() ? "Playlist" : playlist.name,
                         354.f, 1.28f),
                 {72.f, 528.f}, m_font, textPrimary(), 1.28f);
    ren.drawText("Playlist locale", {74.f, 568.f}, m_font, accent(), 0.96f);
    ren.drawText(fitText(m_font, info, 354.f, kHomeMinTextScale),
                 {74.f, 601.f}, m_font, textSecondary(0.82f), kHomeMinTextScale);

    const float listShift = (1.f - reveal) * 74.f;
    const nxui::Rect panel{472.f + listShift, 110.f, 758.f, 506.f};
    drawSmokedGlassPanel(ren, panel, 24.f, 1.f, 0.045f);

    const float listX = panel.x + 24.f;
    ren.drawText("Morceaux", {listX, 132.f}, m_font, textPrimary(), 1.08f);
    ren.drawText("Durée", {1130.f + listShift, 136.f}, m_font,
                 textSecondary(0.76f), kHomeMinTextScale);
    constexpr int rows = 6;
    constexpr float rowPitch = 64.f;
    constexpr float rowHeight = 58.f;
    constexpr float listY = 178.f;
    if (playlist.trackIds.empty()) {
        ren.drawText("Playlist vide", {listX, 218.f}, m_font, textSecondary(), 0.94f);
    } else {
        const float startRow = visibleListStartVisual(playlist.trackIds.size(), rows);
        const int first = std::max(0, static_cast<int>(std::floor(startRow)));
        const int last = std::min(static_cast<int>(playlist.trackIds.size()) - 1,
                                  static_cast<int>(std::ceil(startRow + rows)));
        ren.pushClipRect({listX, listY, 694.f, rows * rowPitch - 6.f});
        for (int idx = first; idx <= last; ++idx) {
            const Track* track = trackForId(playlist.trackIds[static_cast<size_t>(idx)]);
            if (!track) continue;
            const float rowY = listY + (static_cast<float>(idx) - startRow) * rowPitch;
            drawTrackRow(ren, *track, {listX, rowY, 686.f, rowHeight},
                         idx == m_selection, idx + 1);
        }
        ren.popClipRect();
        if (playlist.trackIds.size() > static_cast<size_t>(rows)) {
            constexpr float railY = listY + 2.f, railH = 374.f;
            const float thumbH = std::max(48.f, railH * rows / static_cast<float>(playlist.trackIds.size()));
            const float ratio = startRow / static_cast<float>(playlist.trackIds.size() - rows);
            ren.drawRoundedRect({1210.f + listShift, railY, 3.f, railH}, textSecondary(0.12f), 1.5f);
            ren.drawRoundedRect({1210.f + listShift, railY + (railH - thumbH) * ratio, 3.f, thumbH},
                                accent(0.72f), 1.5f);
        }
    }
    gMusicUiAlpha = saved;
}

void MusicScreen::drawQueue(nxui::Renderer& ren) {
    ren.drawText("À suivre", {65.f, 105.f}, m_font, textPrimary(), 1.15f);
    const auto& ids = m_client.queueTrackIds();
    if (ids.empty()) {
        drawEmpty(ren, "File d’attente vide", "Lance un album ou une playlist pour remplir la file.");
        return;
    }

    constexpr int rows = 7;
    constexpr float rowPitch = 60.f;
    constexpr float rowHeight = 54.f;
    const float start = visibleListStartVisual(ids.size(), rows);
    const int first = std::max(0, static_cast<int>(std::floor(start)));
    const int last = std::min(static_cast<int>(ids.size()) - 1,
                              static_cast<int>(std::ceil(start + rows)));
    ren.pushClipRect({70.f, 142.f, 1132.f, rows * rowPitch - 5.f});
    for (int idx = first; idx <= last; ++idx) {
        const Track* track = trackForId(ids[static_cast<size_t>(idx)]);
        if (!track) continue;
        const float rowY = 142.f + (static_cast<float>(idx) - start) * rowPitch;
        nxui::Rect row{70.f, rowY, 1128.f, rowHeight};
        drawTrackRow(ren, *track, row, idx == m_selection, idx + 1);
        if (ids[static_cast<size_t>(idx)] == m_status.track_id)
            ren.drawRoundedRect({row.x + 3.f, row.y + 6.f, 4.f, row.height - 12.f}, accent(), 2.f);
    }
    ren.popClipRect();

    if (ids.size() > static_cast<size_t>(rows)) {
        constexpr float railY = 143.f, railH = 414.f;
        const float thumbH = std::max(46.f, railH * rows / static_cast<float>(ids.size()));
        const float ratio = start / static_cast<float>(ids.size() - rows);
        ren.drawRoundedRect({1214.f, railY, 3.f, railH}, textSecondary(0.12f), 1.5f);
        ren.drawRoundedRect({1214.f, railY + (railH - thumbH) * ratio, 3.f, thumbH},
                            textSecondary(0.55f), 1.5f);
    }
}

void MusicScreen::drawBottomHints(nxui::Renderer& ren) {
    if (!m_font || m_view == View::Albums || m_view == View::Playlists) return;
    using Hint = std::pair<nxui::Button,std::string>;

    auto drawHintAt = [&](float x, float y, nxui::Button button, const std::string& label) {
        if (m_iconFont) {
            ren.drawText(buttonGlyph(button), {x, y}, m_iconFont, textPrimary(), 0.97f);
            x += 30.f;
        }
        ren.drawText(label, {x, y + 1.f}, m_font, textSecondary(0.94f), kHomeMinTextScale);
    };

    // The two reference screens use a wide console-style footer rather than a
    // centred compact hint cluster.
    if (m_modal == Modal::None && m_view == View::AlbumDetail) {
        ren.drawRect({42.f, 624.f, 1196.f, 1.f},
                     {0.82f, 0.85f, 0.90f, 0.18f * gMusicUiAlpha});
        drawHintAt(44.f,   648.f, nxui::Button::B,     "Retour");
        drawHintAt(318.f,  648.f, nxui::Button::Minus, "Lecture en cours");
        drawHintAt(680.f,  648.f, nxui::Button::Y,     "Ajouter à la playlist");
        drawHintAt(1110.f, 648.f, nxui::Button::A,     "Lire");
        return;
    }

    if (m_modal == Modal::None && m_view == View::NowPlaying) {
        ren.drawRect({42.f, 624.f, 1196.f, 1.f},
                     {0.82f, 0.85f, 0.90f, 0.18f * gMusicUiAlpha});
        drawHintAt(44.f,   648.f, nxui::Button::B, "Retour");
        drawHintAt(582.f,  648.f, nxui::Button::Y, "À suivre");
        drawHintAt(1110.f, 648.f, nxui::Button::A, "Action");
        return;
    }

    static const std::array<Hint,4> keyboardHints = {{
        {nxui::Button::A,"Saisir"},{nxui::Button::X,"Effacer"},
        {nxui::Button::Plus,"Valider"},{nxui::Button::B,"Annuler"}}};
    static const std::array<Hint,2> chooserHints = {{
        {nxui::Button::A,"Ajouter"},{nxui::Button::B,"Annuler"}}};
    static const std::array<Hint,3> playlistDetailHints = {{
        {nxui::Button::A,"Lire"},{nxui::Button::Minus,"Lecture en cours"},
        {nxui::Button::B,"Retour"}}};
    static const std::array<Hint,2> genericHints = {{
        {nxui::Button::A,"Lire"},{nxui::Button::B,"Retour"}}};

    const Hint* hints = nullptr;
    size_t hintCount = 0;
    if (m_modal == Modal::PlaylistNameKeyboard) {
        hints = keyboardHints.data(); hintCount = keyboardHints.size();
    } else if (m_modal == Modal::PlaylistChooser) {
        hints = chooserHints.data(); hintCount = chooserHints.size();
    } else if (m_view == View::PlaylistDetail) {
        hints = playlistDetailHints.data(); hintCount = playlistDetailHints.size();
    } else {
        hints = genericHints.data(); hintCount = genericHints.size();
    }

    float totalW = 0.f;
    for (size_t i = 0; i < hintCount; ++i)
        totalW += 34.f + std::max(76.f, m_font->measure(hints[i].second).x * kHomeMinTextScale + 22.f);
    float x = std::max(42.f, (kScreenW - totalW) * 0.5f);
    for (size_t i = 0; i < hintCount; ++i) {
        const auto& h = hints[i];
        if (m_iconFont) {
            ren.drawText(buttonGlyph(h.first), {x,kBottomY}, m_iconFont, textPrimary(), 0.97f);
            x += 29.f;
        }
        ren.drawText(h.second, {x,kBottomY+1.f}, m_font, textSecondary(), kHomeMinTextScale);
        x += std::max(76.f,m_font->measure(h.second).x*kHomeMinTextScale+22.f);
    }
}

void MusicScreen::drawModal(nxui::Renderer& ren) {
    if (m_modal == Modal::None || !m_font) return;
    ren.drawRect({0,0,kScreenW,kScreenH}, {0.f,0.f,0.f,0.62f * gMusicUiAlpha});
    nxui::Rect p{180.f, 115.f, 920.f, 480.f};
    const nxui::LiquidGlassSettings modalGlass = ren.liquidGlassSettings();
    switchu::homeui::applyLiquidGlassV105(ren);
    ren.captureToOffscreen(true);
    ren.drawLiquidGlass(0, p, 26.f, {0.50f,0.58f,0.72f,0.14f},
                        0.94f * gMusicUiAlpha, 0.f);
    ren.drawRoundedRectOutline(p, {1.f,1.f,1.f,0.16f * gMusicUiAlpha}, 26.f, 1.2f);
    ren.liquidGlassSettings() = modalGlass;

    if (m_modal == Modal::PlaylistChooser) {
        ren.drawText("Ajouter à une playlist", {220.f, 148.f}, m_font, textPrimary(), 1.08f);
        const auto& ps = m_playlistStore.playlists();
        const int start = ps.size() <= 7 ? 0 : std::clamp(m_modalSelection - 3, 0, static_cast<int>(ps.size()) - 7);
        for (int i = 0; i < 7; ++i) {
            const int idx = start + i;
            if (idx >= static_cast<int>(ps.size())) break;
            nxui::Rect r{225.f, 205.f + i * 50.f, 830.f, 43.f};
            if (idx == m_modalSelection) ren.drawRoundedRect(r, panel2(), 11.f);
            ren.drawText(fitText(m_font, ps[static_cast<size_t>(idx)].name, 790.f, kHomeMinTextScale),
                         {r.x + 14.f,r.y + 9.f}, m_font, textPrimary(), kHomeMinTextScale);
        }
        return;
    }

    ren.drawText("Nom de la playlist", {220.f, 148.f}, m_font, textPrimary(), 1.04f);
    ren.drawRoundedRect({220.f, 191.f, 840.f, 52.f}, {0.095f,0.105f,0.125f,0.96f * gMusicUiAlpha}, 13.f);
    const std::string shown = m_keyboardText.empty() ? std::string("…") : fitText(m_font, m_keyboardText, 804.f, 0.86f);
    ren.drawText(shown, {238.f,205.f}, m_font,
                 m_keyboardText.empty() ? textSecondary() : textPrimary(), 0.86f);

    static const std::array<std::string,42> keys = {
        "A","B","C","D","E","F","G","H","I","J",
        "K","L","M","N","O","P","Q","R","S","T",
        "U","V","W","X","Y","Z","0","1","2","3",
        "4","5","6","7","8","9","Espace","-","_","'",".","←"
    };
    constexpr int cols = 10;
    for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
        const int col = i % cols, row = i / cols;
        nxui::Rect r{225.f + col * 82.f, 270.f + row * 58.f, 72.f, 48.f};
        if (i == m_keyboardIndex) ren.drawRoundedRect(r, accent(0.14f), 10.f);
        else ren.drawRoundedRect(r, {0.095f,0.105f,0.125f,0.88f * gMusicUiAlpha}, 10.f);
        const std::string keyText = fitText(m_font, keys[static_cast<size_t>(i)],
                                            r.width - 12.f, kHomeMinTextScale);
        const auto keySize = m_font->measure(keyText);
        ren.drawText(keyText,
                     {r.x + (r.width - keySize.x * kHomeMinTextScale) * 0.5f, r.y + 11.f},
                     m_font, i == m_keyboardIndex ? accent() : textPrimary(),
                     kHomeMinTextScale);
    }
}

void MusicScreen::onRender(nxui::Renderer& ren) {
    if (!m_font || !m_active) return;

    gMusicUiAlpha = std::clamp(m_transitionAlpha, 0.f, 1.f);
    gMusicAccent = kMusicAccent;
    drawMusicBackground(ren);
    drawTopBar(ren);

    if (m_scanRunning) {
        drawEmpty(ren, "Analyse de la bibliothèque…",
                  std::to_string(m_tracksFound.load(std::memory_order_relaxed)) +
                  " morceaux détectés  •  " +
                  std::to_string(m_filesVisited.load(std::memory_order_relaxed)) +
                  " fichiers parcourus");
    } else {
        switch (m_view) {
            case View::Albums: drawAlbums(ren); break;
            case View::Playlists: drawPlaylists(ren); break;
            case View::AlbumDetail: drawAlbumDetail(ren); break;
            case View::PlaylistDetail: drawPlaylistDetail(ren); break;
            case View::NowPlaying: drawNowPlaying(ren); break;
            case View::Queue: drawQueue(ren); break;
        }
    }

    if (m_nextToastTimer > 0.f && currentTrack() && !m_client.queueTrackIds().empty()) {
        const int nextIndex = m_status.current_index + 1;
        if (nextIndex >= 0 && nextIndex < static_cast<int>(m_client.queueTrackIds().size())) {
            const Track* next = trackForId(m_client.queueTrackIds()[static_cast<size_t>(nextIndex)]);
            if (next) {
                nxui::Rect toast{796.f, 92.f, 422.f, 102.f};
                ren.drawRoundedRect(toast, {0.055f,0.060f,0.075f,0.94f * gMusicUiAlpha}, 19.f);
                ren.drawRoundedRectOutline(toast, accent(0.22f), 19.f, 1.2f);
                ren.drawText("À suivre", {toast.x + 18.f,toast.y + 11.f}, m_font, accent(), kHomeMinTextScale);
                ren.drawText(fitText(m_font, next->title, 382.f, 0.92f),
                             {toast.x + 18.f,toast.y + 39.f}, m_font, textPrimary(), 0.92f);
                ren.drawText(fitText(m_font, next->artist, 382.f, kHomeMinTextScale),
                             {toast.x + 18.f,toast.y + 72.f}, m_font, textSecondary(), kHomeMinTextScale);
            }
        }
    }

    drawBottomHints(ren);
    drawModal(ren);
    gMusicUiAlpha = 1.f;
}

void MusicScreen::handleTouch(nxui::Input& input) {
    if (!m_active) return;

    // Mirror the HOME V10.30 touch contract at the Music root.
    constexpr float kScrollStartThreshold = 16.f;
    constexpr float kHorizontalIntentRatio = 1.05f;
    constexpr float kTapMoveThreshold = 18.f;
    constexpr float kCarouselTouchTop = 170.f;
    constexpr float kCarouselTouchBottom = 540.f;

    auto resetRootTouch = [this]() {
        m_touchStartedInCarousel = false;
        m_touchScrollActive = false;
        m_touchLastX = 0.f;
        m_touchLastDuration = 0.f;
        m_touchScrollVelocity = 0.f;
    };

    auto rootHitIndex = [this](float x, float y) -> int {
        if (!rootView()) return -1;
        const int count = rootItemCount();
        if (count <= 0) return -1;
        const int first = std::max(0, static_cast<int>(std::floor(m_rootCarouselMotion.position)) - 3);
        const int last = std::min(count - 1, static_cast<int>(std::ceil(m_rootCarouselMotion.position)) + 3);
        for (int idx = first; idx <= last; ++idx) {
            const float delta = static_cast<float>(idx) - m_rootCarouselMotion.position;
            const float size = switchu::homeui::carouselIconSizeForDistance(delta);
            const float cx = 640.f + switchu::homeui::carouselCenterOffset(delta);
            const nxui::Rect r{cx - size * 0.5f,
                               kMusicCarouselBaselineY - size,
                               size, size};
            if (x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height)
                return idx;
        }
        return -1;
    };

    if (input.touchDown()) {
        m_idleTime = 0.f;
        m_touchTracking = true;
        m_touchStartX = input.touchX();
        m_touchStartY = input.touchY();
        m_touchLastX = m_touchStartX;
        m_touchLastDuration = 0.f;
        m_touchScrollVelocity = 0.f;

        m_touchTimelineScrub = m_modal == Modal::None &&
            m_view == View::NowPlaying && !contentTransitionBusy() &&
            m_touchStartX >= 748.f && m_touchStartX <= 1174.f &&
            m_touchStartY >= 340.f && m_touchStartY <= 390.f;

        m_touchStartedInCarousel = m_modal == Modal::None && rootView() &&
            !contentTransitionBusy() &&
            m_touchStartY >= kCarouselTouchTop &&
            m_touchStartY <= kCarouselTouchBottom;
        m_touchScrollActive = false;
        return;
    }

    if (m_touchTracking && input.isTouching() && rootView() && m_touchStartedInCarousel) {
        const float totalDx = input.touchDeltaX();
        const float totalDy = input.touchDeltaY();
        const bool horizontalGesture =
            std::abs(totalDx) >= kScrollStartThreshold &&
            std::abs(totalDx) > std::abs(totalDy) * kHorizontalIntentRatio;

        if (!m_touchScrollActive && horizontalGesture &&
            switchu::homeui::canTouchCarousel(rootItemCount())) {
            m_touchScrollActive = true;
            switchu::homeui::beginCarouselTouch(m_rootCarouselMotion, rootItemCount());
        }

        if (m_touchScrollActive) {
            const float currentX = input.touchX();
            const float currentDuration = input.touchDuration();
            const float frameDx = currentX - m_touchLastX;
            const float frameDt = currentDuration - m_touchLastDuration;
            m_touchLastX = currentX;
            m_touchLastDuration = currentDuration;

            if (frameDt > 0.001f && frameDt < 0.10f) {
                const float instantVelocity = frameDx / frameDt;
                m_touchScrollVelocity =
                    m_touchScrollVelocity * 0.62f + instantVelocity * 0.38f;
            }
            switchu::homeui::dragCarouselTouch(
                m_rootCarouselMotion, frameDx, rootItemCount());
            return;
        }
    }

    if (!input.touchUp() || !m_touchTracking) return;

    m_touchTracking = false;
    const float x = input.touchX();
    const float y = input.touchY();
    const float dx = input.touchDeltaX();
    const float dy = input.touchDeltaY();

    if (m_modal != Modal::None) {
        m_touchTimelineScrub = false;
        resetRootTouch();
        return;
    }

    if (m_touchScrollActive) {
        switchu::homeui::endCarouselTouch(
            m_rootCarouselMotion, m_touchScrollVelocity, rootItemCount());
        m_touchTimelineScrub = false;
        resetRootTouch();
        return;
    }

    // A Now Playing timeline gesture remains a true scrub even when it moves
    // farther than the ordinary tap threshold.
    if (m_touchTimelineScrub) {
        m_touchTimelineScrub = false;
        resetRootTouch();
        if (m_status.duration_ms > 0) {
            const float ratio = clamp01((x - 762.f) / 397.f);
            const uint64_t target = static_cast<uint64_t>(
                static_cast<double>(m_status.duration_ms) * static_cast<double>(ratio));
            m_nowControl = 5;
            if (m_client.seekMs(target))
                m_status.position_ms = target;
        }
        return;
    }

    if (m_closing || contentTransitionBusy()) {
        resetRootTouch();
        return;
    }

    // HOME behaviour: a short tap may change the selected cover, but never
    // performs A/Ouvrir. Opening remains a physical-controller action.
    if (rootView() && m_touchStartedInCarousel &&
        std::abs(dx) < kTapMoveThreshold && std::abs(dy) < kTapMoveThreshold) {
        const int hit = rootHitIndex(x, y);
        if (hit >= 0 && hit != m_selection) {
            m_selection = hit;
            m_rootInfoReveal = 0.f;
            retargetRootCarousel(false);
        }
        resetRootTouch();
        return;
    }

    resetRootTouch();

    // Tracklists and queue retain direct vertical swipe navigation.
    const bool listView = m_view == View::AlbumDetail ||
                          m_view == View::PlaylistDetail ||
                          m_view == View::Queue;
    if (listView && std::abs(dy) > 48.f && std::abs(dy) > std::abs(dx)) {
        const int steps = std::clamp(static_cast<int>(std::abs(dy) / 62.f), 1, 4);
        m_selection += dy < 0.f ? steps : -steps;
        clampSelectionForView();
        return;
    }

    // Remaining non-root interactions are taps; never reinterpret a swipe as
    // a transport control or track activation.
    if (std::abs(dx) > 22.f || std::abs(dy) > 22.f) return;

    if (m_view == View::NowPlaying) {
        if (y >= 415.f && y <= 507.f) {
            static constexpr std::array<float,5> kNowCenters =
                {735.f, 840.f, 945.f, 1050.f, 1165.f};
            for (int i = 0; i < 5; ++i) {
                const float halfW = i == 2 ? 48.f : 38.f;
                const float cx = kNowCenters[static_cast<size_t>(i)];
                if (x >= cx - halfW && x <= cx + halfW) {
                    m_nowControl = i;
                    activateNowPlayingControl();
                    return;
                }
            }
        }
        return;
    }

    if (m_view == View::AlbumDetail) {
        const size_t count = m_detailAlbum < m_library.albums.size()
            ? m_library.albums[m_detailAlbum].tracks.size() : 0;
        constexpr float listX = 740.f;
        constexpr float listY = 173.f;
        constexpr float rowPitch = 61.f;
        constexpr float rowHeight = 56.f;
        if (x >= listX && x <= 1232.f && y >= listY && y <= 606.f) {
            const float start = visibleListStartVisual(count, 7);
            const int idx = static_cast<int>(std::floor((y - listY) / rowPitch + start));
            if (idx >= 0 && idx < static_cast<int>(count)) {
                const float rowY = listY + (static_cast<float>(idx) - start) * rowPitch;
                if (y >= rowY && y <= rowY + rowHeight) {
                    m_selection = idx;
                    m_listVisualSelection = static_cast<float>(idx);
                    activateSelection();
                }
            }
        }
        return;
    }

    if (m_view == View::PlaylistDetail) {
        const size_t count = m_detailPlaylist < m_playlistStore.playlists().size()
            ? m_playlistStore.playlists()[m_detailPlaylist].trackIds.size() : 0;
        if (x >= 490.f && x <= 1210.f && y >= 178.f) {
            const float start = visibleListStartVisual(count, 6);
            const int idx = static_cast<int>(std::floor((y - 178.f) / 64.f + start));
            if (idx >= 0 && idx < static_cast<int>(count)) {
                const float rowY = 178.f + (static_cast<float>(idx) - start) * 64.f;
                if (y >= rowY && y <= rowY + 58.f) {
                    m_selection = idx;
                    m_listVisualSelection = static_cast<float>(idx);
                    activateSelection();
                }
            }
        }
        return;
    }

    if (m_view == View::Queue) {
        const auto ids = m_client.queueTrackIds();
        constexpr float queueListY = 142.f;
        constexpr float queueRowPitch = 60.f;
        constexpr float queueRowHeight = 54.f;
        if (x >= 70.f && x <= 1198.f && y >= queueListY) {
            const float start = visibleListStartVisual(ids.size(), 7);
            const int idx = static_cast<int>(std::floor(
                (y - queueListY) / queueRowPitch + start));
            if (idx >= 0 && idx < static_cast<int>(ids.size())) {
                const float rowY = queueListY +
                    (static_cast<float>(idx) - start) * queueRowPitch;
                if (y >= rowY && y <= rowY + queueRowHeight) {
                    m_selection = idx;
                    m_listVisualSelection = static_cast<float>(idx);
                    activateSelection();
                }
            }
        }
    }
}

} // namespace switchu::menu::music
