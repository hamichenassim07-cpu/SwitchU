#include "MusicScreen.hpp"

#include "core/DebugLog.hpp"
#include <nxui/core/Renderer.hpp>
#include <switch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace switchu::menu::music {
namespace {

constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kBottomY = 681.f;
constexpr float kRootCoverY = 166.f;
constexpr float kRootSelectedSize = 310.f;
constexpr float kRootNeighborSize = 230.f;
constexpr float kRootSpacing = 284.f;
constexpr float kHomeTabAnimDuration = 0.32f;
const nxui::Color kMusicAccent {0.54f, 0.72f, 1.00f, 1.f};

// V0.02: Albums / Playlists are secondary Music views. The real HOME
// hierarchy stays visually present above them: Jeux / Applications / Musique.
constexpr int kTabCount = 2;

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

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float smoothStep(float v) {
    v = clamp01(v);
    return v * v * (3.f - 2.f * v);
}
float easeOutBackHome(float t) {
    t = clamp01(t);
    constexpr float c1 = 1.45f;
    constexpr float c3 = c1 + 1.f;
    const float x = t - 1.f;
    return 1.f + c3 * x * x * x + c1 * x * x;
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


MusicScreen::~MusicScreen() {
    if (m_scanRunning && m_scanFuture.valid())
        m_scanFuture.wait();
}

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

    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (m_modal != Modal::None) {
            modalMove(0, -1);
            return;
        }
        if (m_view == View::Albums || m_view == View::Playlists) {
            setTab(m_tabIndex - 1);
            return;
        }
        moveSelection(0, -1);
    });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (m_modal != Modal::None) {
            modalMove(0, 1);
            return;
        }
        if (m_view == View::Albums || m_view == View::Playlists) {
            setTab(m_tabIndex + 1);
            return;
        }
        moveSelection(0, 1);
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
    m_tabIndex = 0;
    m_selection = 0;
    m_carouselVisualIndex = 0.f;
    m_listVisualSelection = 0.f;
    m_detailTransition = 1.f;
    m_detailClosing = false;
    m_nowPlayingEnter = 1.f;
    m_nowPlayingClosing = false;
    m_nowPlayingReturnSelection = 0;
    m_queueReturnSelection = 0;
    m_homeTabSlide = 1.f;
    m_homeTabAnimFrom = 1.f;
    m_homeTabAnimTo = 2.f;
    m_homeTabAnimTime = 0.f;
    m_homeTabAnimating = true;
    m_rootInfoReveal = 1.f;
    m_modal = Modal::None;
    m_client.loadQueueTrackIds();
    refreshStatus(true);
    if (!m_hasScanned) startScan(false);
    DebugLog::log("[music-diag] OPEN_MUSIC ready scan=%d", m_scanRunning ? 1 : 0);
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
    DebugLog::log("[music-diag] SCAN_LIBRARY begin root=%s", switchu::music::kRootDirectory);
    m_filesVisited.store(0);
    m_tracksFound.store(0);
    m_scanRunning = true;
    m_scanFuture = std::async(std::launch::async, [this]() {
        return MusicLibrary::scan(switchu::music::kRootDirectory,
                                  &m_filesVisited, &m_tracksFound);
    });
}


void MusicScreen::finishScanIfReady() {
    if (!m_scanRunning || !m_scanFuture.valid()) return;
    if (m_scanFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    // Preserve a stable root identity across a rescan instead of trusting the
    // previous vector index, whose ordering can legitimately change.
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

    // A library refresh is allowed to remove the container currently open.
    // Fall back to its root instead of leaving stale indices in the renderer.
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
    if (rootView()) m_carouselVisualIndex = static_cast<float>(m_selection);
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
        if (fresh.track_id != previousTrackId)
            DebugLog::log("[music-diag] STATUS track changed %016llX -> %016llX",
                          static_cast<unsigned long long>(previousTrackId),
                          static_cast<unsigned long long>(fresh.track_id));

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


void MusicScreen::updateBattery(float dt) {
    m_batteryTimer += dt;
    if (m_batteryTimer < 1.f) return;
    m_batteryTimer = 0.f;
    u32 charge = 100;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&charge)))
        m_batteryPercent = std::min<u32>(100, charge);
    PsmChargerType type = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&type)))
        m_batteryCharging = type != PsmChargerType_Unconnected;
}

void MusicScreen::onUpdate(float dt) {
    m_uiTime += dt;
    m_statusTimer += dt;
    if (m_nextToastTimer > 0.f) m_nextToastTimer = std::max(0.f, m_nextToastTimer - dt);
    // Dark Music uses one stable system accent; covers never recolour the UI.
    gMusicAccent = kMusicAccent;

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
    m_transitionAlpha = std::min(1.f, m_transitionAlpha + dt / 0.30f);

    // Use the exact HOME V10.30 category-lens motion on entry: out-back
    // position with a brief squeeze/release handled in drawTopBar().
    if (m_homeTabAnimating) {
        m_homeTabAnimTime += std::max(0.f, dt);
        const float u = clamp01(m_homeTabAnimTime / kHomeTabAnimDuration);
        const float eased = easeOutBackHome(u);
        m_homeTabSlide = m_homeTabAnimFrom +
            (m_homeTabAnimTo - m_homeTabAnimFrom) * eased;
        if (u >= 1.f) {
            m_homeTabSlide = m_homeTabAnimTo;
            m_homeTabAnimating = false;
        }
    } else {
        m_homeTabSlide = 2.f;
    }

    if (m_view == View::Albums || m_view == View::Playlists)
        m_rootInfoReveal = std::min(1.f, m_rootInfoReveal + std::max(0.f, dt) / 0.18f);
    if (m_closing) {
        m_transitionAlpha = std::max(0.f, m_transitionAlpha - dt / 0.22f);
        if (m_transitionAlpha <= 0.f) {
            m_closing = false;
            if (m_closeCb) m_closeCb(); else hide();
            return;
        }
    }

    // HOME-derived motion: fast enough for a console UI, but never a hard
    // snap. Repeated direction presses retarget the current visual position.
    const float carouselBlend = 1.f - std::exp(-std::max(0.f, dt) * 13.0f);
    m_carouselVisualIndex += (static_cast<float>(m_selection) - m_carouselVisualIndex) * carouselBlend;
    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail || m_view == View::Queue) {
        const float listBlend = 1.f - std::exp(-std::max(0.f, dt) * 18.0f);
        m_listVisualSelection +=
            (static_cast<float>(m_selection) - m_listVisualSelection) * listBlend;
    }

    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail) {
        const float speed = std::max(0.f, dt) / 0.24f;
        if (m_detailClosing) {
            m_detailTransition = std::max(0.f, m_detailTransition - speed);
            if (m_detailTransition <= 0.f) {
                m_detailClosing = false;
                m_view = m_detailReturnView;
                m_selection = m_view == View::Albums
                    ? static_cast<int>(m_detailAlbum)
                    : static_cast<int>(m_detailPlaylist);
                m_carouselVisualIndex = static_cast<float>(m_selection);
            }
        } else {
            m_detailTransition = std::min(1.f, m_detailTransition + speed);
        }
    }

    if (m_view == View::NowPlaying) {
        if (m_nowPlayingClosing) {
            m_nowPlayingEnter = std::max(0.f, m_nowPlayingEnter - std::max(0.f, dt) / 0.18f);
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
            m_nowPlayingEnter = std::min(1.f, m_nowPlayingEnter + std::max(0.f, dt) / 0.22f);
        }
    }

    finishScanIfReady();
    refreshStatus(false);
    updateBattery(dt);
}


void MusicScreen::setTab(int index) {
    if (!rootView() || m_closing || contentTransitionBusy()) return;
    index = (index % kTabCount + kTabCount) % kTabCount;
    if (m_tabIndex == index &&
        ((index == 0 && m_view == View::Albums) || (index == 1 && m_view == View::Playlists)))
        return;
    m_tabIndex = index;
    m_selection = 0;
    m_carouselVisualIndex = 0.f;
    m_rootInfoReveal = 0.f;
    m_view = index == 0 ? View::Albums : View::Playlists;
}


bool MusicScreen::rootView() const {
    return m_view == View::Albums || m_view == View::Playlists;
}

bool MusicScreen::contentTransitionBusy() const {
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
    if (m_closing || contentTransitionBusy()) return;

    if (rootView()) {
        if (dx != 0) {
            const int before = m_selection;
            m_selection += dx;
            clampSelectionForView();
            if (m_selection != before) m_rootInfoReveal = 0.f;
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

    m_selection += dy != 0 ? dy : dx;
    clampSelectionForView();
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
        appendToQueue(albumTrackIds(m_detailAlbum));
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
    m_tabIndex = 1;
    m_view = View::Playlists;
    m_selection = static_cast<int>(created);
    m_carouselVisualIndex = static_cast<float>(m_selection);
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

std::string MusicScreen::formatClock() const {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char out[16]{};
    std::strftime(out, sizeof(out), "%H:%M", &local);
    return out;
}

std::string MusicScreen::fitText(nxui::Font* font, const std::string& text,
                                 float maxWidth, float scale) const {
    if (!font || text.empty() || maxWidth <= 0.f || scale <= 0.f)
        return text;
    if (font->measure(text).x * scale <= maxWidth)
        return text;

    // Truncate on UTF-8 codepoint boundaries so accented names never end in a
    // broken byte sequence. Binary search keeps this cheap even for long tags.
    std::vector<size_t> ends;
    ends.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = 1;
        if ((c & 0xE0u) == 0xC0u) len = 2;
        else if ((c & 0xF0u) == 0xE0u) len = 3;
        else if ((c & 0xF8u) == 0xF0u) len = 4;
        i = std::min(text.size(), i + len);
        ends.push_back(i);
    }

    const std::string ellipsis = "…";
    if (font->measure(ellipsis).x * scale > maxWidth)
        return {};

    size_t lo = 0, hi = ends.size();
    while (lo < hi) {
        const size_t mid = (lo + hi + 1) / 2;
        const std::string candidate = (mid == 0 ? std::string{} : text.substr(0, ends[mid - 1])) + ellipsis;
        if (font->measure(candidate).x * scale <= maxWidth)
            lo = mid;
        else
            hi = mid - 1;
    }
    return (lo == 0 ? std::string{} : text.substr(0, ends[lo - 1])) + ellipsis;
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

void MusicScreen::drawReflectedCover(nxui::Renderer& ren, const CoverRef& cover,
                                     const nxui::Rect& r, bool selected, int maxSide) {
    const nxui::Texture* tex = m_coverCache.get(cover, ren, maxSide);
    if (!tex) {
        drawCover(ren, cover, r, selected, maxSide);
        return;
    }

    ren.drawTextureRounded(tex, r, 14.f, {1.f, 1.f, 1.f, gMusicUiAlpha});
    if (selected) {
        ren.drawRoundedRectOutline(r.expanded(4.f), accent(0.42f), 18.f, 5.5f);
        ren.drawRoundedRectOutline(r.expanded(1.5f), {1.f, 1.f, 1.f, 0.92f * gMusicUiAlpha}, 16.f, 2.f);
    }

    // iPod-style floor reflection: vertically flipped texture rendered as
    // thin strips with a rapid alpha falloff. No extra texture allocation.
    constexpr int strips = 14;
    const float reflH = std::min(92.f, r.height * 0.27f);
    const float yBase = r.y + r.height + 11.f;
    const int slot = tex->descriptorSlot();
    if (slot >= 0) {
        for (int i = 0; i < strips; ++i) {
            const float t0 = static_cast<float>(i) / strips;
            const float t1 = static_cast<float>(i + 1) / strips;
            const float y0 = yBase + reflH * t0;
            const float y1 = yBase + reflH * t1;
            const float vTop = 1.f - t0;
            const float vBottom = 1.f - t1;
            const float alpha = (0.145f * std::pow(1.f - t0, 2.25f)) * gMusicUiAlpha;

            // Cheap three-tap blur approximation. The reflection stays soft
            // without allocating another texture or invoking a heavy blur
            // pass on Switch hardware. The taps sum to the original alpha.
            constexpr float offsets[3] = {-2.2f, 0.f, 2.2f};
            constexpr float weights[3] = {0.22f, 0.56f, 0.22f};
            for (int tap = 0; tap < 3; ++tap) {
                const nxui::Color tint{1.f, 1.f, 1.f, alpha * weights[tap]};
                const float ox = offsets[tap];
                const nxui::Vec2 p0{r.x + ox, y0}, p1{r.x + r.width + ox, y0};
                const nxui::Vec2 p2{r.x + r.width + ox, y1}, p3{r.x + ox, y1};
                ren.drawTexturedTriangle(slot, p0, {0.f, vTop}, p1, {1.f, vTop}, p2, {1.f, vBottom}, tint);
                ren.drawTexturedTriangle(slot, p0, {0.f, vTop}, p2, {1.f, vBottom}, p3, {0.f, vBottom}, tint);
            }
        }
    }
}

void MusicScreen::drawCrtBackground(nxui::Renderer& ren) {
    const float a = gMusicUiAlpha;

    // Stable dark Music environment. No cover-driven colour fields and no
    // decorative circles: depth comes from restrained tonal layers and a
    // subtle audio/rack rhythm.
    ren.drawGradientRect({0.f, 0.f, kScreenW, kScreenH},
                         {0.020f, 0.023f, 0.030f, a},
                         {0.010f, 0.012f, 0.017f, a});

    // Very soft horizontal equipment lines / waveform rhythm.
    for (int y = 104; y < 505; y += 34)
        ren.drawRect({0.f, static_cast<float>(y), kScreenW, 1.f},
                     {0.52f, 0.57f, 0.66f, 0.015f * a});
    for (int x = 72; x < 1280; x += 142)
        ren.drawRect({static_cast<float>(x), 94.f, 1.f, 400.f},
                     {0.50f, 0.56f, 0.66f, 0.010f * a});

    // Minimal audio pulse line, fixed neutral colour and intentionally faint.
    nxui::Vec2 prev{0.f, 418.f};
    for (int x = 12; x <= 1280; x += 12) {
        const float xf = static_cast<float>(x);
        const float wave = std::sin(xf * 0.031f + m_uiTime * 0.24f) * 4.0f +
                           std::sin(xf * 0.011f - m_uiTime * 0.11f) * 2.0f;
        nxui::Vec2 cur{xf, 418.f + wave};
        ren.drawLine(prev, cur, {0.55f, 0.64f, 0.78f, 0.030f * a}, 1.f);
        prev = cur;
    }

    // Reflective floor: a real visual plane, not a mirror. Covers add their
    // own faded reflection on top of this surface.
    ren.drawGradientRect({0.f, 492.f, kScreenW, 228.f},
                         {0.075f, 0.082f, 0.096f, 0.30f * a},
                         {0.020f, 0.022f, 0.030f, 0.96f * a});
    ren.drawRect({0.f, 492.f, kScreenW, 1.f}, {0.72f, 0.77f, 0.86f, 0.11f * a});
}


void MusicScreen::drawTopBar(nxui::Renderer& ren) {
    if (!m_font || !m_smallFont) return;

    // HOME V10.30 clock rhythm, including the fixed 800 ms separator blink.
    const std::string clockText = formatClock();
    const std::size_t colon = clockText.find(':');
    const float clockX = 19.f, clockY = 25.f, clockScale = 1.28f;
    const nxui::Color clockShadow{0.f, 0.f, 0.f, 0.34f * gMusicUiAlpha};
    const auto drawClockPiece = [&](const std::string& text, float x) {
        ren.drawText(text, {x + 1.1f, clockY + 1.2f}, m_font, clockShadow, clockScale);
        ren.drawText(text, {x, clockY}, m_font, textPrimary(), clockScale);
        ren.drawText(text, {x + 0.55f, clockY}, m_font, textPrimary(0.52f), clockScale);
    };
    if (colon != std::string::npos) {
        const std::string hh = clockText.substr(0, colon);
        const std::string mm = clockText.substr(colon + 1);
        const float hhW = m_font->measure(hh).x * clockScale;
        const float colonW = m_font->measure(":").x * clockScale;
        const auto blinkMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        drawClockPiece(hh, clockX);
        if (((blinkMs / 800) % 2) == 0)
            drawClockPiece(":", clockX + hhW);
        drawClockPiece(mm, clockX + hhW + colonW);
    } else {
        drawClockPiece(clockText, clockX);
    }

    const float px = 202.f, py = 45.f;
    if (m_profileTexture && m_profileTexture->valid()) {
        ren.drawTextureRounded(m_profileTexture, {px - 27.f, py - 27.f, 54.f, 54.f}, 27.f,
                               {1.f, 1.f, 1.f, gMusicUiAlpha});
        ren.drawRoundedRectOutline({px - 28.f, py - 28.f, 56.f, 56.f},
                                   {1.f,1.f,1.f,0.64f * gMusicUiAlpha}, 28.f, 1.2f);
    } else {
        ren.drawCircle({px, py}, 26.f, {0.18f, 0.20f, 0.25f, 0.98f * gMusicUiAlpha}, 48);
        ren.drawCircle({px, py - 6.f}, 6.f, textPrimary(0.86f), 24);
        ren.drawRoundedRect({px - 10.f, py + 4.f, 20.f, 10.f}, textPrimary(0.72f), 5.f);
    }

    constexpr float navX = 380.f, navY = 18.f, navW = 520.f, navH = 54.f;
    constexpr float inset = 7.f, gap = 6.f;
    constexpr float tabW = (navW - inset * 2.f - gap * 2.f) / 3.f;
    constexpr float tabH = 40.f;
    const nxui::Rect nav{navX, navY, navW, navH};
    const float gamesX = navX + inset;
    const float step = tabW + gap;

    // Exact HOME V10.30 category trajectory. Music is the third native tab,
    // so the lens continues from Applications (1) to Music (2) with the same
    // elastic out-back motion and squeeze/release used by DateTimeWidget.
    const float slide = m_homeTabSlide;
    nxui::Rect active{gamesX + step * slide, navY + inset, tabW, tabH};
    if (m_homeTabAnimating) {
        const float u = clamp01(m_homeTabAnimTime / kHomeTabAnimDuration);
        if (u < 0.34f) {
            const float phase = u / 0.34f;
            const float shrinkX = 22.f * phase;
            const float pinchY = 4.f * phase;
            active.x += shrinkX * 0.5f;
            active.width -= shrinkX;
            active.y += pinchY * 0.5f;
            active.height -= pinchY;
        } else {
            const float phase = (u - 0.34f) / 0.66f;
            const float release = 1.f - std::pow(1.f - std::min(1.f, phase), 2.f);
            const float shrinkX = 22.f * (1.f - release);
            const float pinchY = 4.f * (1.f - release);
            active.x += shrinkX * 0.5f;
            active.width -= shrinkX;
            active.y += pinchY * 0.5f;
            active.height -= pinchY;
        }
    }

    ren.captureToOffscreen(true);
    ren.drawLiquidGlass(0, nav, 24.f,
                        {0.70f, 0.82f, 0.98f, 0.34f},
                        0.98f * gMusicUiAlpha, 0.f);
    ren.drawRoundedRectOutline(nav, {0.98f, 1.00f, 1.00f, 0.32f * gMusicUiAlpha}, 24.f, 1.f);
    ren.drawLiquidGlass(0, active, 18.f,
                        {1.f, 1.f, 1.f, 0.94f},
                        0.98f * gMusicUiAlpha, 0.f);
    ren.drawRoundedRect(active.shrunk(1.4f), {1.f,1.f,1.f,0.60f * gMusicUiAlpha}, 16.8f);
    ren.drawRoundedRectOutline(active, {1.f,1.f,1.f,0.88f * gMusicUiAlpha}, 18.f, 1.2f);

    const std::array<std::string,3> labels = {"Jeux", "Applications", "Musique"};
    for (int i = 0; i < 3; ++i) {
        const float proximity = 1.f - std::clamp(std::abs(slide - static_cast<float>(i)), 0.f, 1.f);
        const nxui::Color inactive{0.985f,0.99f,1.f,0.96f * gMusicUiAlpha};
        const nxui::Color selected{0.045f,0.052f,0.065f,0.98f * gMusicUiAlpha};
        const nxui::Color c{
            inactive.r + (selected.r - inactive.r) * proximity,
            inactive.g + (selected.g - inactive.g) * proximity,
            inactive.b + (selected.b - inactive.b) * proximity,
            inactive.a
        };
        const auto sz = m_smallFont->measure(labels[static_cast<size_t>(i)]);
        const float cx = gamesX + step * i + tabW * 0.5f;
        ren.drawText(labels[static_cast<size_t>(i)], {cx - sz.x * 0.5f, navY + 17.f}, m_smallFont, c, 1.f);
    }

    if (m_iconFont) {
        const std::string lg = buttonGlyph(nxui::Button::L);
        const std::string rg = buttonGlyph(nxui::Button::R);
        ren.drawText(lg, {344.f, 33.f}, m_iconFont, textPrimary(0.94f), 0.88f);
        ren.drawText(rg, {916.f, 33.f}, m_iconFont, textPrimary(0.46f), 0.88f);
    }

    // Music intentionally omits Wi-Fi. Battery + percentage retain HOME scale.
    const float level = std::clamp(m_batteryPercent / 100.f, 0.f, 1.f);
    nxui::Rect body{1097.f, 32.f, 50.f, 24.f};
    ren.drawRoundedRectOutline(body, textPrimary(0.92f), 5.8f, 1.65f);
    ren.drawRoundedRect({1149.f, 38.5f, 4.5f, 11.f}, textPrimary(0.76f), 1.8f);
    nxui::Color bc = level <= 0.20f ? nxui::Color{1.00f,0.24f,0.22f,0.96f * gMusicUiAlpha}
                    : level <= 0.50f ? nxui::Color{1.00f,0.78f,0.18f,0.96f * gMusicUiAlpha}
                    : nxui::Color{0.28f,0.92f,0.42f,0.96f * gMusicUiAlpha};
    nxui::Rect fill = body.shrunk(3.4f);
    fill.width *= level;
    if (fill.width > 0.5f) {
        if (!m_batteryCharging) {
            ren.drawRoundedRect(fill, bc, std::min(4.f, fill.width * 0.5f));
        } else {
            // Same moving pink/violet/blue/cyan family as HOME V10.30, but
            // without the Wi-Fi element that Music intentionally removes.
            const std::array<nxui::Color,7> stops = {{
                {1.00f,0.18f,0.70f,0.96f * gMusicUiAlpha},
                {0.68f,0.27f,1.00f,0.96f * gMusicUiAlpha},
                {0.22f,0.43f,1.00f,0.96f * gMusicUiAlpha},
                {0.10f,0.82f,1.00f,0.96f * gMusicUiAlpha},
                {0.22f,0.43f,1.00f,0.96f * gMusicUiAlpha},
                {0.68f,0.27f,1.00f,0.96f * gMusicUiAlpha},
                {1.00f,0.18f,0.70f,0.96f * gMusicUiAlpha}
            }};
            auto mix = [](const nxui::Color& a, const nxui::Color& b, float t) {
                t = clamp01(t);
                return nxui::Color{a.r + (b.r-a.r)*t, a.g + (b.g-a.g)*t,
                                   a.b + (b.b-a.b)*t, a.a + (b.a-a.a)*t};
            };
            constexpr int samples = 64;
            const float sampleW = fill.width / samples;
            const float phase = std::fmod(m_uiTime * 0.055f, 1.f);
            ren.pushClipRect(fill);
            for (int i = 0; i < samples; ++i) {
                float u = (static_cast<float>(i) + 0.5f) / samples;
                float wrapped = std::fmod(u * 1.18f - phase + 4.f, 1.f);
                float scaled = wrapped * 6.f;
                int idx = std::min(5, static_cast<int>(std::floor(scaled)));
                float local = scaled - static_cast<float>(idx);
                local = local * local * (3.f - 2.f * local);
                nxui::Color c = mix(stops[static_cast<size_t>(idx)], stops[static_cast<size_t>(idx) + 1], local);
                ren.drawRect({fill.x + sampleW * i - 0.3f, fill.y, sampleW + 0.6f, fill.height}, c);
            }
            ren.popClipRect();
        }
    }
    ren.drawText(std::to_string(m_batteryPercent) + "%", {1163.f, 30.f}, m_font,
                 textPrimary(), 0.98f);

    drawSecondaryMusicTabs(ren);
}


void MusicScreen::drawSecondaryMusicTabs(nxui::Renderer& ren) {
    if (!m_smallFont || (m_view != View::Albums && m_view != View::Playlists)) return;
    const std::array<std::string,2> labels = {"Albums", "Playlists"};
    constexpr float centers[2] = {588.f, 692.f};
    for (int i = 0; i < 2; ++i) {
        const auto sz = m_smallFont->measure(labels[static_cast<size_t>(i)]);
        const bool active = i == m_tabIndex;
        ren.drawText(labels[static_cast<size_t>(i)], {centers[i] - sz.x * 0.42f, 82.f}, m_smallFont,
                     active ? textPrimary(0.94f) : textSecondary(0.62f), 0.84f);
        if (active)
            ren.drawRoundedRect({centers[i] - 18.f, 99.f, 36.f, 2.f}, accent(0.86f), 1.f);
    }
}

void MusicScreen::drawCover(nxui::Renderer& ren, const CoverRef& cover,
                            const nxui::Rect& r, bool selected, int maxSide) {
    const nxui::Texture* tex = m_coverCache.get(cover, ren, maxSide);
    if (tex) {
        ren.drawTextureRounded(tex, r, 14.f, {1.f,1.f,1.f,gMusicUiAlpha});
    } else {
        ren.drawRoundedRect(r, {0.095f, 0.105f, 0.125f, 0.96f * gMusicUiAlpha}, 14.f);
        if (m_font) {
            const auto m = m_font->measure("♪");
            ren.drawText("♪", {r.x + (r.width - m.x * 1.6f) * 0.5f,
                               r.y + (r.height - m.y * 1.6f) * 0.5f - 4.f},
                         m_font, textSecondary(0.62f), 1.6f);
        }
    }
    if (selected) {
        ren.drawRoundedRectOutline(r.expanded(4.f), accent(0.34f), 19.f, 6.f);
        ren.drawRoundedRectOutline(r.expanded(1.f), {1.f,1.f,1.f,0.96f * gMusicUiAlpha}, 16.f, 2.f);
    }
}


void MusicScreen::drawEmpty(nxui::Renderer& ren, const std::string& title,
                            const std::string& detail) {
    if (!m_font || !m_smallFont) return;
    ren.drawText(fitText(m_font, title, 1120.f, 1.50f),
                 {78.f, 285.f}, m_font, textPrimary(), 1.50f);
    ren.drawText(fitText(m_smallFont, detail, 1120.f, 0.94f),
                 {80.f, 340.f}, m_smallFont, textSecondary(), 0.94f);
}

void MusicScreen::drawAlbums(nxui::Renderer& ren) {
    if (m_library.albums.empty()) {
        drawEmpty(ren, "Aucun album", "Ajoute des fichiers MP3 ou FLAC dans sdmc:/Music/.");
        return;
    }
    m_selection = std::clamp(m_selection, 0, static_cast<int>(m_library.albums.size()) - 1);

    for (int idx = std::max(0, m_selection - 3);
         idx <= std::min(static_cast<int>(m_library.albums.size()) - 1, m_selection + 3); ++idx) {
        const float delta = static_cast<float>(idx) - m_carouselVisualIndex;
        if (std::abs(delta) > 2.65f) continue;
        const float focus = std::clamp(1.f - std::abs(delta), 0.f, 1.f);
        const float size = kRootNeighborSize + (kRootSelectedSize - kRootNeighborSize) * focus;
        const float cx = 640.f + delta * kRootSpacing;
        const float y = kRootCoverY + (1.f - focus) * 52.f;
        const float alpha = std::clamp(1.f - std::max(0.f, std::abs(delta) - 1.f) * 0.34f, 0.40f, 1.f);
        const float saved = gMusicUiAlpha;
        gMusicUiAlpha *= alpha;
        drawReflectedCover(ren, m_library.albums[static_cast<size_t>(idx)].cover,
                           {cx - size * 0.5f, y, size, size}, idx == m_selection,
                           focus > 0.55f ? 512 : 320);
        gMusicUiAlpha = saved;
    }

    const float saved = gMusicUiAlpha;
    gMusicUiAlpha *= smoothStep(m_rootInfoReveal);
    const auto& album = m_library.albums[static_cast<size_t>(m_selection)];
    const std::string title = fitText(m_font, album.title.empty() ? "Album sans titre" : album.title, 860.f, 1.38f);
    const std::string artist = fitText(m_smallFont, album.artist.empty() ? "Artiste inconnu" : album.artist, 760.f, 0.94f);
    const auto titleSize = m_font->measure(title);
    const auto artistSize = m_smallFont->measure(artist);
    ren.drawText(title, {640.f - titleSize.x * 1.38f * 0.5f, 526.f}, m_font, textPrimary(), 1.38f);
    ren.drawText(artist, {640.f - artistSize.x * 0.94f * 0.5f, 570.f}, m_smallFont, textSecondary(), 0.94f);

    ren.drawRect({405.f, 607.f, 470.f, 1.f}, {0.78f,0.82f,0.90f,0.14f * gMusicUiAlpha});
    ren.drawText("◷  " + formatDuration(albumDurationMs(static_cast<size_t>(m_selection))),
                 {431.f, 626.f}, m_smallFont, textSecondary(), 0.80f);
    const bool playable = !album.tracks.empty();
    if (m_iconFont) {
        ren.drawText(buttonGlyph(nxui::Button::X), {593.f, 622.f}, m_iconFont,
                     playable ? textPrimary() : textSecondary(0.34f), 0.78f);
        ren.drawText(buttonGlyph(nxui::Button::A), {742.f, 622.f}, m_iconFont, textPrimary(), 0.78f);
    }
    ren.drawText("Lire", {622.f, 626.f}, m_smallFont,
                 playable ? textSecondary() : textSecondary(0.34f), 0.78f);
    ren.drawText("Ouvrir", {771.f, 626.f}, m_smallFont, textSecondary(), 0.78f);
    gMusicUiAlpha = saved;
}

int MusicScreen::visibleListStart(size_t count, int rows) const {
    if (count <= static_cast<size_t>(rows)) return 0;
    int start = m_selection - rows / 2;
    return std::clamp(start, 0, static_cast<int>(count) - rows);
}

float MusicScreen::visibleListStartVisual(size_t count, int rows) const {
    if (count <= static_cast<size_t>(rows)) return 0.f;
    const float maxStart = static_cast<float>(count - static_cast<size_t>(rows));
    return std::clamp(m_listVisualSelection - static_cast<float>(rows / 2), 0.f, maxStart);
}

void MusicScreen::drawNowPlayingIndicator(nxui::Renderer& ren, const nxui::Rect& row) {
    const int frame = static_cast<int>(m_uiTime * 10.f) & 3;
    const float x = row.x + 42.f;
    const float y = row.y + 12.f;

    // Asset hook for the requested runner. The repository intentionally ships
    // no Nintendo/Mario art. A user-approved 4-frame horizontal sprite sheet
    // can later be placed at this path without touching playback or layout.
    if (!m_runnerLoadAttempted) {
        m_runnerLoadAttempted = true;
        m_nowPlayingRunnerTexture.loadFromFile(
            ren.gpu(), ren, "romfs:/icons/music_now_playing_runner.png", 128);
    }
    if (m_nowPlayingRunnerTexture.valid()) {
        const int slot = m_nowPlayingRunnerTexture.descriptorSlot();
        if (slot >= 0) {
            constexpr float frameW = 26.f;
            constexpr float frameH = 26.f;
            const float u0 = static_cast<float>(frame) * 0.25f;
            const float u1 = u0 + 0.25f;
            const nxui::Color tint{1.f, 1.f, 1.f, 0.98f * gMusicUiAlpha};
            const nxui::Vec2 p0{x, y}, p1{x + frameW, y};
            const nxui::Vec2 p2{x + frameW, y + frameH}, p3{x, y + frameH};
            ren.drawTexturedTriangle(slot, p0, {u0,0.f}, p1, {u1,0.f}, p2, {u1,1.f}, tint);
            ren.drawTexturedTriangle(slot, p0, {u0,0.f}, p2, {u1,1.f}, p3, {u0,1.f}, tint);
            return;
        }
    }

    // Legal-safe fallback until the final asset decision is made.
    const nxui::Color c = accent(0.96f);
    ren.drawRect({x + 5.f, y, 5.f, 5.f}, c);
    ren.drawRect({x + 4.f, y + 5.f, 7.f, 7.f}, c);
    ren.drawRect({x + 2.f, y + 7.f, 3.f, 3.f}, c);
    ren.drawRect({x + 11.f, y + 7.f, 3.f, 3.f}, c);
    if (frame < 2) {
        ren.drawRect({x + 3.f, y + 12.f, 3.f, 5.f}, c);
        ren.drawRect({x + 10.f, y + 12.f, 3.f, 3.f}, c);
    } else {
        ren.drawRect({x + 2.f, y + 12.f, 5.f, 3.f}, c);
        ren.drawRect({x + 10.f, y + 12.f, 3.f, 5.f}, c);
    }
}

void MusicScreen::drawTrackRow(nxui::Renderer& ren, const Track& t, const nxui::Rect& row,
                               bool selected, int ordinal) {
    if (selected) {
        ren.drawRoundedRect(row, {0.115f,0.126f,0.150f,0.97f * gMusicUiAlpha}, 13.f);
        ren.drawRoundedRectOutline(row, accent(0.36f), 13.f, 1.2f);
    }
    if (ordinal > 0) {
        char num[12]{};
        std::snprintf(num, sizeof(num), "%d", ordinal);
        ren.drawText(num, {row.x + 17.f, row.y + 15.f}, m_smallFont,
                     selected ? accent() : textSecondary(), 0.80f);
    }

    const bool playing = hasMusicSession() && m_status.track_id == t.id;
    if (playing) drawNowPlayingIndicator(ren, row);

    const float titleX = row.x + (playing ? 72.f : 56.f);
    const std::string duration = formatDuration(t.durationMs);
    const float durationW = m_smallFont ? m_smallFont->measure(duration).x * 0.76f : 52.f;
    const float durationX = row.x + row.width - durationW - 16.f;
    const float maxTitleW = std::max(40.f, durationX - titleX - 18.f);
    const std::string title = fitText(m_smallFont,
                                      t.title.empty() ? "Morceau sans titre" : t.title,
                                      maxTitleW, 0.90f);
    ren.drawText(title, {titleX, row.y + 12.f}, m_smallFont,
                 selected ? textPrimary() : textPrimary(0.94f), 0.90f);
    ren.drawText(duration, {durationX, row.y + 14.f}, m_smallFont,
                 textSecondary(), 0.76f);
}

void MusicScreen::drawPlaylists(nxui::Renderer& ren) {
    const auto& playlists = m_playlistStore.playlists();
    if (playlists.empty()) {
        drawEmpty(ren, "Aucune playlist", "Appuie sur + pour créer ta première playlist locale.");
        return;
    }
    m_selection = std::clamp(m_selection, 0, static_cast<int>(playlists.size()) - 1);

    for (int idx = std::max(0, m_selection - 3);
         idx <= std::min(static_cast<int>(playlists.size()) - 1, m_selection + 3); ++idx) {
        const float delta = static_cast<float>(idx) - m_carouselVisualIndex;
        if (std::abs(delta) > 2.65f) continue;
        const float focus = std::clamp(1.f - std::abs(delta), 0.f, 1.f);
        const float size = kRootNeighborSize + (kRootSelectedSize - kRootNeighborSize) * focus;
        const float cx = 640.f + delta * kRootSpacing;
        const float y = kRootCoverY + (1.f - focus) * 52.f;
        const float alpha = std::clamp(1.f - std::max(0.f, std::abs(delta) - 1.f) * 0.34f, 0.40f, 1.f);
        const float saved = gMusicUiAlpha;
        gMusicUiAlpha *= alpha;
        drawReflectedCover(ren, playlistCover(static_cast<size_t>(idx)),
                           {cx - size * 0.5f, y, size, size}, idx == m_selection,
                           focus > 0.55f ? 512 : 320);
        gMusicUiAlpha = saved;
    }

    const float saved = gMusicUiAlpha;
    gMusicUiAlpha *= smoothStep(m_rootInfoReveal);
    const auto& playlist = playlists[static_cast<size_t>(m_selection)];
    const std::string title = fitText(m_font,
                                      playlist.name.empty() ? "Playlist" : playlist.name,
                                      860.f, 1.38f);
    const auto titleSize = m_font->measure(title);
    ren.drawText(title, {640.f - titleSize.x * 1.38f * 0.5f, 526.f}, m_font, textPrimary(), 1.38f);
    const std::string sub = std::to_string(playlist.trackIds.size()) + " morceaux";
    const auto subSize = m_smallFont->measure(sub);
    ren.drawText(sub, {640.f - subSize.x * 0.92f * 0.5f, 570.f}, m_smallFont, textSecondary(), 0.92f);

    ren.drawRect({405.f, 607.f, 470.f, 1.f}, {0.78f,0.82f,0.90f,0.14f * gMusicUiAlpha});
    ren.drawText("◷  " + formatDuration(playlistDurationMs(static_cast<size_t>(m_selection))),
                 {431.f, 626.f}, m_smallFont, textSecondary(), 0.80f);
    const bool playable = !playlist.trackIds.empty();
    if (m_iconFont) {
        ren.drawText(buttonGlyph(nxui::Button::X), {593.f, 622.f}, m_iconFont,
                     playable ? textPrimary() : textSecondary(0.34f), 0.78f);
        ren.drawText(buttonGlyph(nxui::Button::A), {742.f, 622.f}, m_iconFont, textPrimary(), 0.78f);
    }
    ren.drawText("Lire", {622.f, 626.f}, m_smallFont,
                 playable ? textSecondary() : textSecondary(0.34f), 0.78f);
    ren.drawText("Ouvrir", {771.f, 626.f}, m_smallFont, textSecondary(), 0.78f);
    gMusicUiAlpha = saved;
}

void MusicScreen::drawAlbumDetail(nxui::Renderer& ren) {
    if (m_detailAlbum >= m_library.albums.size()) {
        drawEmpty(ren, "Album indisponible", "La bibliothèque a changé. Reviens au carrousel.");
        return;
    }
    const auto& album = m_library.albums[m_detailAlbum];
    const float e = smoothStep(clamp01(m_detailTransition));
    const float rootFade = (1.f - e) * (1.f - e);

    if (rootFade > 0.002f) {
        const float contextSaved = gMusicUiAlpha;
        for (int idx = std::max(0, static_cast<int>(m_detailAlbum) - 2);
             idx <= std::min(static_cast<int>(m_library.albums.size()) - 1,
                             static_cast<int>(m_detailAlbum) + 2); ++idx) {
            if (idx == static_cast<int>(m_detailAlbum)) continue;
            const float delta = static_cast<float>(idx) - static_cast<float>(m_detailAlbum);
            const float focus = clamp01(1.f - std::abs(delta));
            const float size = kRootNeighborSize + (kRootSelectedSize - kRootNeighborSize) * focus;
            float cx = 640.f + delta * kRootSpacing;
            cx += (delta < 0.f ? -1.f : 1.f) * 46.f * e;
            const float y = kRootCoverY + (1.f - focus) * 52.f + 14.f * e;
            gMusicUiAlpha = contextSaved * rootFade * (std::abs(delta) > 1.f ? 0.64f : 0.90f);
            drawReflectedCover(ren, m_library.albums[static_cast<size_t>(idx)].cover,
                               {cx - size * 0.5f, y, size, size}, false, 320);
        }

        gMusicUiAlpha = contextSaved * rootFade;
        const std::string rootTitle = fitText(m_font, album.title, 860.f, 1.38f);
        const std::string rootArtist = fitText(m_smallFont, album.artist, 760.f, 0.94f);
        const auto rootTitleSize = m_font->measure(rootTitle);
        const auto rootArtistSize = m_smallFont->measure(rootArtist);
        ren.drawText(rootTitle, {640.f - rootTitleSize.x * 1.38f * 0.5f, 526.f}, m_font, textPrimary(), 1.38f);
        ren.drawText(rootArtist, {640.f - rootArtistSize.x * 0.94f * 0.5f, 570.f}, m_smallFont, textSecondary(), 0.94f);
        ren.drawRect({405.f, 607.f, 470.f, 1.f}, {0.78f,0.82f,0.90f,0.14f * gMusicUiAlpha});
        ren.drawText("◷  " + formatDuration(albumDurationMs(m_detailAlbum)),
                     {431.f, 626.f}, m_smallFont, textSecondary(), 0.80f);
        if (m_iconFont) {
            ren.drawText(buttonGlyph(nxui::Button::X), {593.f, 622.f}, m_iconFont, textPrimary(), 0.78f);
            ren.drawText(buttonGlyph(nxui::Button::A), {742.f, 622.f}, m_iconFont, textPrimary(), 0.78f);
        }
        ren.drawText("Lire", {622.f, 626.f}, m_smallFont, textSecondary(), 0.78f);
        ren.drawText("Ouvrir", {771.f, 626.f}, m_smallFont, textSecondary(), 0.78f);
        const std::array<std::string,2> ghostTabs = {"Albums", "Playlists"};
        constexpr float ghostCenters[2] = {588.f, 692.f};
        for (int ti = 0; ti < 2; ++ti) {
            const auto ts = m_smallFont->measure(ghostTabs[static_cast<size_t>(ti)]);
            const bool activeTab = ti == 0;
            ren.drawText(ghostTabs[static_cast<size_t>(ti)], {ghostCenters[ti] - ts.x * 0.42f, 82.f},
                         m_smallFont, activeTab ? textPrimary(0.94f) : textSecondary(0.62f), 0.84f);
            if (activeTab)
                ren.drawRoundedRect({ghostCenters[ti] - 18.f, 99.f, 36.f, 2.f}, accent(0.86f), 1.f);
        }
        gMusicUiAlpha = contextSaved;
    }

    const nxui::Rect start{640.f - kRootSelectedSize * 0.5f, kRootCoverY,
                           kRootSelectedSize, kRootSelectedSize};
    const nxui::Rect end{86.f, 154.f, 330.f, 330.f};
    const nxui::Rect cover{
        start.x + (end.x - start.x) * e,
        start.y + (end.y - start.y) * e,
        start.width + (end.width - start.width) * e,
        start.height + (end.height - start.height) * e
    };
    drawReflectedCover(ren, album.cover, cover, false, 512);

    const float saved = gMusicUiAlpha;
    const float contentReveal = smoothStep((e - 0.16f) / 0.84f);
    gMusicUiAlpha *= contentReveal;
    uint64_t totalMs = albumDurationMs(m_detailAlbum);
    std::string info = formatDuration(totalMs) + "  •  " + std::to_string(album.tracks.size()) + " morceaux";
    if (album.year > 0) info += "  •  " + std::to_string(album.year);
    ren.drawText(fitText(m_font, album.title.empty() ? "Album sans titre" : album.title, 330.f, 1.28f),
                 {86.f, 505.f}, m_font, textPrimary(), 1.28f);
    ren.drawText(fitText(m_smallFont, album.artist.empty() ? "Artiste inconnu" : album.artist, 330.f, 0.88f),
                 {88.f, 548.f}, m_smallFont, textSecondary(), 0.88f);
    ren.drawText(fitText(m_smallFont, info, 330.f, 0.70f),
                 {88.f, 580.f}, m_smallFont, textSecondary(0.76f), 0.70f);

    const float listShift = (1.f - contentReveal) * 54.f;
    const float listX = 500.f + listShift;
    ren.drawText("Morceaux", {listX, 126.f}, m_font, textPrimary(), 1.08f);
    ren.drawText("Durée", {1132.f + listShift, 132.f}, m_smallFont, textSecondary(0.58f), 0.64f);
    constexpr int rows = 7;
    if (album.tracks.empty()) {
        ren.drawText("Aucun morceau", {listX, 214.f}, m_font, textSecondary(0.86f), 0.94f);
    } else {
        const float startRow = visibleListStartVisual(album.tracks.size(), rows);
        const int first = std::max(0, static_cast<int>(std::floor(startRow)));
        const int last = std::min(static_cast<int>(album.tracks.size()) - 1,
                                  static_cast<int>(std::ceil(startRow + rows)));
        ren.pushClipRect({listX, 169.f, 704.f, rows * 58.f - 6.f});
        for (int idx = first; idx <= last; ++idx) {
            const size_t ti = album.tracks[static_cast<size_t>(idx)];
            if (ti >= m_library.tracks.size()) continue;
            const Track& track = m_library.tracks[ti];
            const int ordinal = track.trackNumber > 0 ? track.trackNumber : idx + 1;
            const float rowY = 169.f + (static_cast<float>(idx) - startRow) * 58.f;
            drawTrackRow(ren, track, {listX, rowY, 700.f, 52.f}, idx == m_selection, ordinal);
        }
        ren.popClipRect();
        if (album.tracks.size() > static_cast<size_t>(rows)) {
            constexpr float railY = 170.f, railH = 400.f;
            const float thumbH = std::max(46.f, railH * rows / static_cast<float>(album.tracks.size()));
            const float ratio = startRow / static_cast<float>(album.tracks.size() - rows);
            ren.drawRoundedRect({1214.f + listShift, railY, 3.f, railH}, textSecondary(0.12f), 1.5f);
            ren.drawRoundedRect({1214.f + listShift, railY + (railH - thumbH) * ratio, 3.f, thumbH},
                                textSecondary(0.55f), 1.5f);
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
    const float e = smoothStep(clamp01(m_detailTransition));
    const float rootFade = (1.f - e) * (1.f - e);

    if (rootFade > 0.002f) {
        const auto& playlists = m_playlistStore.playlists();
        const float contextSaved = gMusicUiAlpha;
        for (int idx = std::max(0, static_cast<int>(m_detailPlaylist) - 2);
             idx <= std::min(static_cast<int>(playlists.size()) - 1,
                             static_cast<int>(m_detailPlaylist) + 2); ++idx) {
            if (idx == static_cast<int>(m_detailPlaylist)) continue;
            const float delta = static_cast<float>(idx) - static_cast<float>(m_detailPlaylist);
            const float focus = clamp01(1.f - std::abs(delta));
            const float size = kRootNeighborSize + (kRootSelectedSize - kRootNeighborSize) * focus;
            float cx = 640.f + delta * kRootSpacing;
            cx += (delta < 0.f ? -1.f : 1.f) * 46.f * e;
            const float y = kRootCoverY + (1.f - focus) * 52.f + 14.f * e;
            gMusicUiAlpha = contextSaved * rootFade * (std::abs(delta) > 1.f ? 0.64f : 0.90f);
            drawReflectedCover(ren, playlistCover(static_cast<size_t>(idx)),
                               {cx - size * 0.5f, y, size, size}, false, 320);
        }
        gMusicUiAlpha = contextSaved * rootFade;
        const std::string rootTitle = fitText(m_font, playlist.name, 860.f, 1.38f);
        const auto rootTitleSize = m_font->measure(rootTitle);
        ren.drawText(rootTitle, {640.f - rootTitleSize.x * 1.38f * 0.5f, 526.f},
                     m_font, textPrimary(), 1.38f);
        const std::string rootSub = std::to_string(playlist.trackIds.size()) + " morceaux";
        const auto rootSubSize = m_smallFont->measure(rootSub);
        ren.drawText(rootSub, {640.f - rootSubSize.x * 0.92f * 0.5f, 570.f},
                     m_smallFont, textSecondary(), 0.92f);
        ren.drawRect({405.f, 607.f, 470.f, 1.f}, {0.78f,0.82f,0.90f,0.14f * gMusicUiAlpha});
        ren.drawText("◷  " + formatDuration(playlistDurationMs(m_detailPlaylist)),
                     {431.f, 626.f}, m_smallFont, textSecondary(), 0.80f);
        if (m_iconFont) {
            ren.drawText(buttonGlyph(nxui::Button::X), {593.f, 622.f}, m_iconFont, textPrimary(), 0.78f);
            ren.drawText(buttonGlyph(nxui::Button::A), {742.f, 622.f}, m_iconFont, textPrimary(), 0.78f);
        }
        ren.drawText("Lire", {622.f, 626.f}, m_smallFont, textSecondary(), 0.78f);
        ren.drawText("Ouvrir", {771.f, 626.f}, m_smallFont, textSecondary(), 0.78f);
        const std::array<std::string,2> ghostTabs = {"Albums", "Playlists"};
        constexpr float ghostCenters[2] = {588.f, 692.f};
        for (int ti = 0; ti < 2; ++ti) {
            const auto ts = m_smallFont->measure(ghostTabs[static_cast<size_t>(ti)]);
            const bool activeTab = ti == 1;
            ren.drawText(ghostTabs[static_cast<size_t>(ti)], {ghostCenters[ti] - ts.x * 0.42f, 82.f},
                         m_smallFont, activeTab ? textPrimary(0.94f) : textSecondary(0.62f), 0.84f);
            if (activeTab)
                ren.drawRoundedRect({ghostCenters[ti] - 18.f, 99.f, 36.f, 2.f}, accent(0.86f), 1.f);
        }
        gMusicUiAlpha = contextSaved;
    }

    const nxui::Rect start{640.f - kRootSelectedSize * 0.5f, kRootCoverY,
                           kRootSelectedSize, kRootSelectedSize};
    const nxui::Rect end{86.f, 154.f, 330.f, 330.f};
    const nxui::Rect cover{
        start.x + (end.x - start.x) * e,
        start.y + (end.y - start.y) * e,
        start.width + (end.width - start.width) * e,
        start.height + (end.height - start.height) * e
    };
    drawReflectedCover(ren, playlistCover(m_detailPlaylist), cover, false, 512);

    const float saved = gMusicUiAlpha;
    const float contentReveal = smoothStep((e - 0.16f) / 0.84f);
    gMusicUiAlpha *= contentReveal;
    const std::string info = formatDuration(playlistDurationMs(m_detailPlaylist)) + "  •  " +
                             std::to_string(playlist.trackIds.size()) + " morceaux";
    ren.drawText(fitText(m_font, playlist.name.empty() ? "Playlist" : playlist.name, 330.f, 1.28f),
                 {86.f, 505.f}, m_font, textPrimary(), 1.28f);
    ren.drawText("Playlist locale", {88.f, 548.f}, m_smallFont, textSecondary(), 0.88f);
    ren.drawText(fitText(m_smallFont, info, 330.f, 0.70f),
                 {88.f, 580.f}, m_smallFont, textSecondary(0.76f), 0.70f);

    const float listShift = (1.f - contentReveal) * 54.f;
    const float listX = 500.f + listShift;
    ren.drawText("Morceaux", {listX, 126.f}, m_font, textPrimary(), 1.08f);
    ren.drawText("Durée", {1132.f + listShift, 132.f}, m_smallFont, textSecondary(0.58f), 0.64f);
    constexpr int rows = 7;
    if (playlist.trackIds.empty()) {
        ren.drawText("Playlist vide", {listX, 214.f}, m_font, textSecondary(0.86f), 0.94f);
    } else {
        const float startRow = visibleListStartVisual(playlist.trackIds.size(), rows);
        const int first = std::max(0, static_cast<int>(std::floor(startRow)));
        const int last = std::min(static_cast<int>(playlist.trackIds.size()) - 1,
                                  static_cast<int>(std::ceil(startRow + rows)));
        ren.pushClipRect({listX, 169.f, 704.f, rows * 58.f - 6.f});
        for (int idx = first; idx <= last; ++idx) {
            const Track* track = trackForId(playlist.trackIds[static_cast<size_t>(idx)]);
            if (!track) continue;
            const float rowY = 169.f + (static_cast<float>(idx) - startRow) * 58.f;
            drawTrackRow(ren, *track, {listX, rowY, 700.f, 52.f},
                         idx == m_selection, idx + 1);
        }
        ren.popClipRect();
        if (playlist.trackIds.size() > static_cast<size_t>(rows)) {
            constexpr float railY = 170.f, railH = 400.f;
            const float thumbH = std::max(46.f, railH * rows / static_cast<float>(playlist.trackIds.size()));
            const float ratio = startRow / static_cast<float>(playlist.trackIds.size() - rows);
            ren.drawRoundedRect({1214.f + listShift, railY, 3.f, railH}, textSecondary(0.12f), 1.5f);
            ren.drawRoundedRect({1214.f + listShift, railY + (railH - thumbH) * ratio, 3.f, thumbH},
                                textSecondary(0.55f), 1.5f);
        }
    }
    gMusicUiAlpha = saved;
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
    const float shift = (1.f - e) * 24.f;

    drawReflectedCover(ren, track->cover, {82.f, 145.f + shift, 392.f, 392.f}, false, 512);
    ren.drawText("Lecture en cours", {548.f, 132.f + shift}, m_smallFont, textSecondary(), 0.82f);
    ren.drawText(fitText(m_font, track->title.empty() ? "Morceau sans titre" : track->title, 630.f, 1.54f),
                 {546.f, 178.f + shift}, m_font, textPrimary(), 1.54f);
    ren.drawText(fitText(m_font, track->artist.empty() ? "Artiste inconnu" : track->artist, 620.f, 0.96f),
                 {548.f, 234.f + shift}, m_font, accent(), 0.96f);
    ren.drawText(fitText(m_smallFont, track->album.empty() ? "Album inconnu" : track->album, 620.f, 0.84f),
                 {548.f, 274.f + shift}, m_smallFont, textSecondary(), 0.84f);

    const float progress = m_status.duration_ms > 0
        ? std::clamp(float(double(m_status.position_ms) / double(m_status.duration_ms)), 0.f, 1.f) : 0.f;
    const nxui::Rect timeline{548.f, 342.f + shift, 610.f, 12.f};
    if (m_nowControl == 5)
        ren.drawRoundedRectOutline(timeline.expanded(8.f), accent(0.42f), 10.f, 1.5f);
    ren.drawRoundedRect(timeline, {0.20f,0.22f,0.27f,0.90f * gMusicUiAlpha}, 6.f);
    ren.drawRoundedRect({timeline.x, timeline.y, timeline.width * progress, timeline.height}, accent(), 6.f);
    const float knobX = timeline.x + timeline.width * progress;
    ren.drawCircle({knobX, timeline.y + timeline.height * 0.5f}, 7.f, textPrimary(), 24);
    ren.drawText(formatDuration(m_status.position_ms), {548.f, 368.f + shift}, m_smallFont, textSecondary(), 0.74f);
    const std::string duration = formatDuration(m_status.duration_ms);
    const auto durSz = m_smallFont->measure(duration);
    ren.drawText(duration, {1158.f - durSz.x * 0.74f, 368.f + shift}, m_smallFont, textSecondary(), 0.74f);

    const std::array<std::string,5> labels = {
        statusFlag(m_status, switchu::music::MusicStatus_Shuffle) ? "⇄" : "↝",
        "◀◀",
        statusFlag(m_status, switchu::music::MusicStatus_Playing) ? "Ⅱ" : "▶",
        "▶▶",
        repeatLabel(m_status.repeat_mode)
    };
    for (int i = 0; i < 5; ++i) {
        const float x = 574.f + i * 116.f;
        nxui::Rect button{x, 424.f + shift, 76.f, 64.f};
        if (i == m_nowControl) {
            ren.drawRoundedRect(button, panel2(0.96f), 23.f);
            ren.drawRoundedRectOutline(button, accent(0.44f), 23.f, 1.4f);
        }
        ren.drawText(labels[static_cast<size_t>(i)], {button.x + 19.f, button.y + 15.f}, m_font,
                     i == m_nowControl ? textPrimary() : textSecondary(), 0.94f);
    }

    ren.drawText("↑ progression   ←/→ déplacer   ZL -10 s   ZR +10 s",
                 {548.f, 524.f + shift}, m_smallFont, textSecondary(0.64f), 0.66f);
    gMusicUiAlpha = saved;
}

void MusicScreen::drawQueue(nxui::Renderer& ren) {
    ren.drawText("À suivre", {65.f, 105.f}, m_font, textPrimary(), 1.15f);
    const auto& ids = m_client.queueTrackIds();
    if (ids.empty()) {
        drawEmpty(ren, "File d’attente vide", "Lance un album ou une playlist pour remplir la file.");
        return;
    }

    constexpr int rows = 8;
    const float start = visibleListStartVisual(ids.size(), rows);
    const int first = std::max(0, static_cast<int>(std::floor(start)));
    const int last = std::min(static_cast<int>(ids.size()) - 1,
                              static_cast<int>(std::ceil(start + rows)));
    ren.pushClipRect({70.f, 142.f, 1132.f, rows * 54.f - 5.f});
    for (int idx = first; idx <= last; ++idx) {
        const Track* track = trackForId(ids[static_cast<size_t>(idx)]);
        if (!track) continue;
        const float rowY = 142.f + (static_cast<float>(idx) - start) * 54.f;
        nxui::Rect row{70.f, rowY, 1128.f, 49.f};
        drawTrackRow(ren, *track, row, idx == m_selection, idx + 1);
        if (ids[static_cast<size_t>(idx)] == m_status.track_id)
            ren.drawRoundedRect({row.x + 3.f, row.y + 6.f, 4.f, row.height - 12.f}, accent(), 2.f);
    }
    ren.popClipRect();

    if (ids.size() > static_cast<size_t>(rows)) {
        constexpr float railY = 143.f, railH = 424.f;
        const float thumbH = std::max(46.f, railH * rows / static_cast<float>(ids.size()));
        const float ratio = start / static_cast<float>(ids.size() - rows);
        ren.drawRoundedRect({1214.f, railY, 3.f, railH}, textSecondary(0.12f), 1.5f);
        ren.drawRoundedRect({1214.f, railY + (railH - thumbH) * ratio, 3.f, thumbH},
                            textSecondary(0.55f), 1.5f);
    }
}

void MusicScreen::drawBottomHints(nxui::Renderer& ren) {
    if (!m_smallFont || m_view == View::Albums || m_view == View::Playlists) return;
    std::vector<std::pair<nxui::Button,std::string>> hints;
    if (m_modal == Modal::PlaylistNameKeyboard) {
        hints = {{nxui::Button::A,"Saisir"},{nxui::Button::X,"Effacer"},{nxui::Button::Plus,"Valider"},{nxui::Button::B,"Annuler"}};
    } else if (m_modal == Modal::PlaylistChooser) {
        hints = {{nxui::Button::A,"Ajouter"},{nxui::Button::B,"Annuler"}};
    } else if (m_view == View::NowPlaying) {
        hints = {{nxui::Button::A,"Action"},{nxui::Button::Y,"À suivre"},{nxui::Button::B,"Retour"}};
    } else if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail) {
        hints = {{nxui::Button::A,"Lire"},{nxui::Button::Minus,"Lecture en cours"},{nxui::Button::B,"Retour"}};
    } else {
        hints = {{nxui::Button::A,"Lire"},{nxui::Button::B,"Retour"}};
    }
    float totalW = 0.f;
    for (const auto& h : hints)
        totalW += 34.f + std::max(76.f, m_smallFont->measure(h.second).x * 0.68f + 22.f);
    float x = std::max(42.f, (kScreenW - totalW) * 0.5f);
    for (const auto& h : hints) {
        if (m_iconFont) {
            ren.drawText(buttonGlyph(h.first), {x, kBottomY}, m_iconFont, textPrimary(), 0.72f);
            x += 29.f;
        }
        ren.drawText(h.second, {x, kBottomY + 1.f}, m_smallFont, textSecondary(), 0.68f);
        x += std::max(76.f, m_smallFont->measure(h.second).x * 0.68f + 22.f);
    }
}


void MusicScreen::drawModal(nxui::Renderer& ren) {
    if (m_modal == Modal::None || !m_font || !m_smallFont) return;
    ren.drawRect({0,0,kScreenW,kScreenH}, {0.f,0.f,0.f,0.62f * gMusicUiAlpha});
    nxui::Rect p{180.f, 115.f, 920.f, 480.f};
    ren.drawRoundedRect(p, {0.055f,0.060f,0.075f,0.985f * gMusicUiAlpha}, 26.f);
    ren.drawRoundedRectOutline(p, {0.78f,0.82f,0.90f,0.18f * gMusicUiAlpha}, 26.f, 1.4f);

    if (m_modal == Modal::PlaylistChooser) {
        ren.drawText("Ajouter à une playlist", {220.f, 148.f}, m_font, textPrimary(), 1.08f);
        const auto& ps = m_playlistStore.playlists();
        const int start = ps.size() <= 7 ? 0 : std::clamp(m_modalSelection - 3, 0, static_cast<int>(ps.size()) - 7);
        for (int i = 0; i < 7; ++i) {
            const int idx = start + i;
            if (idx >= static_cast<int>(ps.size())) break;
            nxui::Rect r{225.f, 205.f + i * 50.f, 830.f, 43.f};
            if (idx == m_modalSelection) ren.drawRoundedRect(r, panel2(), 11.f);
            ren.drawText(fitText(m_smallFont, ps[static_cast<size_t>(idx)].name, 790.f, 0.76f),
                         {r.x + 14.f,r.y + 11.f}, m_smallFont, textPrimary(), 0.76f);
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
        const float sc = keys[static_cast<size_t>(i)].size() > 2 ? 0.48f : 0.72f;
        ren.drawText(keys[static_cast<size_t>(i)], {r.x + 13.f,r.y + 13.f}, m_smallFont,
                     i == m_keyboardIndex ? accent() : textPrimary(), sc);
    }
}

void MusicScreen::onRender(nxui::Renderer& ren) {
    if (!m_font || !m_smallFont || !m_active) return;

    gMusicUiAlpha = std::clamp(m_transitionAlpha, 0.f, 1.f);
    gMusicAccent = kMusicAccent;
    drawCrtBackground(ren);
    drawTopBar(ren);

    if (m_scanRunning) {
        drawEmpty(ren, "Analyse de la bibliothèque…",
                  std::to_string(m_tracksFound.load()) + " morceaux détectés  •  " +
                  std::to_string(m_filesVisited.load()) + " fichiers parcourus");
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
                nxui::Rect toast{812.f, 92.f, 406.f, 82.f};
                ren.drawRoundedRect(toast, {0.055f,0.060f,0.075f,0.94f * gMusicUiAlpha}, 19.f);
                ren.drawRoundedRectOutline(toast, accent(0.22f), 19.f, 1.2f);
                ren.drawText("À suivre", {toast.x + 18.f,toast.y + 12.f}, m_smallFont, accent(), 0.73f);
                ren.drawText(fitText(m_smallFont, next->title, 364.f, 0.80f),
                             {toast.x + 18.f,toast.y + 36.f}, m_smallFont, textPrimary(), 0.80f);
                ren.drawText(fitText(m_smallFont, next->artist, 364.f, 0.65f),
                             {toast.x + 18.f,toast.y + 59.f}, m_smallFont, textSecondary(), 0.65f);
            }
        }
    }

    drawBottomHints(ren);
    drawModal(ren);
    gMusicUiAlpha = 1.f;
}

void MusicScreen::handleTouch(nxui::Input& input) {
    if (!m_active) return;

    if (input.touchDown()) {
        m_touchTracking = true;
        m_touchStartX = input.touchX();
        m_touchStartY = input.touchY();
        m_touchTimelineScrub = m_modal == Modal::None &&
            m_view == View::NowPlaying && !contentTransitionBusy() &&
            m_touchStartX >= 520.f && m_touchStartX <= 1190.f &&
            m_touchStartY >= 320.f && m_touchStartY <= 390.f;
        return;
    }
    if (!input.touchUp() || !m_touchTracking) return;

    m_touchTracking = false;
    const float x = input.touchX();
    const float y = input.touchY();
    const float dx = x - m_touchStartX;
    const float dy = y - m_touchStartY;

    if (m_modal != Modal::None) {
        m_touchTimelineScrub = false;
        return;
    }

    // A timeline gesture is a scrub even when the finger travelled farther
    // than the normal tap threshold. V2 only sought on taps, which made the
    // visibly draggable progress bar ignore actual drag gestures.
    if (m_touchTimelineScrub) {
        m_touchTimelineScrub = false;
        if (m_status.duration_ms > 0) {
            const float ratio = clamp01((x - 548.f) / 610.f);
            const uint64_t target = static_cast<uint64_t>(
                static_cast<double>(m_status.duration_ms) * static_cast<double>(ratio));
            m_nowControl = 5;
            if (m_client.seekMs(target))
                m_status.position_ms = target;
        }
        return;
    }

    if (m_closing || contentTransitionBusy()) return;

    // Root carousel swipes select only; a separate tap opens the focused item.
    if (rootView() && std::abs(dx) > 55.f && std::abs(dx) > std::abs(dy)) {
        moveSelection(dx < 0.f ? 1 : -1, 0);
        return;
    }

    // Tracklists and queue can be scrolled directly with a vertical swipe.
    const bool listView = m_view == View::AlbumDetail || m_view == View::PlaylistDetail || m_view == View::Queue;
    if (listView && std::abs(dy) > 48.f && std::abs(dy) > std::abs(dx)) {
        const int steps = std::clamp(static_cast<int>(std::abs(dy) / 58.f), 1, 4);
        m_selection += dy < 0.f ? steps : -steps;
        clampSelectionForView();
        return;
    }

    // Remaining interactions are taps; never reinterpret a swipe as a button.
    if (std::abs(dx) > 22.f || std::abs(dy) > 22.f) return;

    if (rootView() && y >= 76.f && y <= 108.f && x >= 520.f && x <= 760.f) {
        setTab(x < 640.f ? 0 : 1);
        return;
    }

    if (m_view == View::NowPlaying) {
        if (y >= 410.f && y <= 504.f) {
            for (int i = 0; i < 5; ++i) {
                const float bx = 574.f + i * 116.f;
                if (x >= bx && x <= bx + 76.f) {
                    m_nowControl = i;
                    activateNowPlayingControl();
                    return;
                }
            }
        }
        return;
    }

    if (rootView() && y >= 150.f && y <= 520.f) {
        // Touching a visible neighbour simply selects it; touching the centre
        // performs the same A/Ouvrir action as the controller.
        if (x < 470.f) moveSelection(-1, 0);
        else if (x > 810.f) moveSelection(1, 0);
        else activateSelection();
        return;
    }

    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail) {
        const size_t count = m_view == View::AlbumDetail && m_detailAlbum < m_library.albums.size()
            ? m_library.albums[m_detailAlbum].tracks.size()
            : (m_detailPlaylist < m_playlistStore.playlists().size()
                ? m_playlistStore.playlists()[m_detailPlaylist].trackIds.size() : 0);
        if (x >= 500.f && x <= 1210.f && y >= 169.f) {
            const float start = visibleListStartVisual(count, 7);
            const int idx = static_cast<int>(std::floor((y - 169.f) / 58.f + start));
            if (idx >= 0 && idx < static_cast<int>(count)) {
                const float rowY = 169.f + (static_cast<float>(idx) - start) * 58.f;
                if (y >= rowY && y <= rowY + 52.f) {
                    m_selection = idx;
                    m_listVisualSelection = static_cast<float>(idx);
                    activateSelection();
                    return;
                }
            }
        }
    }

    if (m_view == View::Queue && x >= 70.f && x <= 1198.f && y >= 142.f) {
        const auto& queueIds = m_client.queueTrackIds();
        const float start = visibleListStartVisual(queueIds.size(), 8);
        const int idx = static_cast<int>(std::floor((y - 142.f) / 54.f + start));
        if (idx >= 0 && idx < static_cast<int>(queueIds.size())) {
            const float rowY = 142.f + (static_cast<float>(idx) - start) * 54.f;
            if (y >= rowY && y <= rowY + 49.f) {
                m_selection = idx;
                m_listVisualSelection = static_cast<float>(idx);
                activateSelection();
            }
        }
    }
}

} // namespace switchu::menu::music
