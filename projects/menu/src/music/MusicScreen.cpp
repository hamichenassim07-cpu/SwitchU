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
#include <unordered_map>
#include <nxui/third_party/stb/stb_image.h>

namespace switchu::menu::music {
namespace {

constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kTopY = 22.f;
constexpr float kContentTop = 106.f;
constexpr float kMiniY = 610.f;
constexpr float kMiniH = 76.f;
constexpr float kBottomY = 684.f;
constexpr int kListRows = 8;

// V0.02: the music space exposes only the two visual categories defined by
// the approved concepts. Artists/tracks/search still exist as supporting
// views, but no longer crowd the primary navigation.
const char* kTabs[] = {"Albums", "Playlists"};
constexpr int kTabCount = 2;

// Render helpers use this frame alpha so HOME <-> Music can cross-fade
// without rebuilding either side of the interface.
float gMusicUiAlpha = 1.f;
nxui::Color gMusicAccent {0.46f, 0.31f, 0.92f, 1.f};

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

nxui::Color textPrimary(float a = 1.f) { return {0.075f, 0.085f, 0.11f, a * gMusicUiAlpha}; }
nxui::Color textSecondary(float a = 1.f) { return {0.31f, 0.33f, 0.38f, a * gMusicUiAlpha}; }
nxui::Color accent(float a = 1.f) {
    return {gMusicAccent.r, gMusicAccent.g, gMusicAccent.b, a * gMusicUiAlpha};
}
nxui::Color panel(float a = 1.f) { return {0.985f, 0.987f, 0.995f, a * gMusicUiAlpha}; }
nxui::Color panel2(float a = 1.f) { return {0.94f, 0.94f, 0.985f, a * gMusicUiAlpha}; }

bool statusFlag(const switchu::music::Status& st, uint32_t flag) {
    return (st.flags & flag) != 0;
}

std::string repeatLabel(uint8_t mode) {
    switch (static_cast<switchu::music::RepeatMode>(mode)) {
        case switchu::music::RepeatMode::Track: return "1";
        case switchu::music::RepeatMode::Queue: return "∞";
        default: return "↻";
    }
}

} // namespace

MusicScreen::MusicScreen() {
    setRect({0.f, 0.f, kScreenW, kScreenH});
    // V0.02 remains mounted while inactive so it can expose the global mini
    // player and guard HOME BGM without recreating GPU/audio state.
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
    if (m_paletteRunning && m_paletteFuture.valid())
        m_paletteFuture.wait();
}

void MusicScreen::setupActions() {
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_modal != Modal::None) modalActivate(); else activateSelection();
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (m_modal != Modal::None) modalCancel(); else goBack();
    });
    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_modal == Modal::SearchKeyboard || m_modal == Modal::PlaylistNameKeyboard)
            modalBackspace();
        else if (m_modal == Modal::None)
            contextualX();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if (m_modal == Modal::None) contextualY();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        if (m_modal == Modal::SearchKeyboard || m_modal == Modal::PlaylistNameKeyboard)
            modalConfirm();
        else if (m_modal == Modal::None)
            openCreatePlaylist();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        if (m_modal == Modal::None && currentTrack()) {
            m_returnView = m_view;
            m_view = View::NowPlaying;
            m_selection = 0;
        }
    });
    addAction(static_cast<uint64_t>(nxui::Button::L), [this]() {
        if (m_modal == Modal::None && m_view <= View::Playlists) setTab(m_tabIndex - 1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::R), [this]() {
        if (m_modal == Modal::None && m_view <= View::Playlists) setTab(m_tabIndex + 1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZL), [this]() {
        if (m_modal == Modal::None && m_view == View::NowPlaying) seekRelative(-10'000);
    });
    addAction(static_cast<uint64_t>(nxui::Button::ZR), [this]() {
        if (m_modal == Modal::None && m_view == View::NowPlaying) seekRelative(10'000);
    });

    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (m_modal != Modal::None) modalMove(0, -1); else moveSelection(0, -1);
    });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (m_modal != Modal::None) modalMove(0, 1); else moveSelection(0, 1);
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
    m_hiddenCoverReleaseTimer = 0.f;
    setVisible(true);
    setOpacity(1.f);
    setFocusable(true);
    m_view = View::Albums;
    m_returnView = View::Albums;
    m_tabIndex = 0;
    m_selection = 0;
    m_homeSection = 0;
    m_modal = Modal::None;
    m_client.loadQueueTrackIds();
    refreshStatus(true);
    if (!m_hasScanned) startScan(false);
    refreshAdaptivePalette();
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
    // Do not destroy GPU covers on the same frame as HOME restoration.
    // Delayed release avoids the V0.01 corrupted/pixel frame on return.
    m_hiddenCoverReleaseTimer = 1.0f;
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

    DebugLog::log("[music-diag] SCAN_LIBRARY commit begin");
    m_library = m_scanFuture.get();
    m_scanRunning = false;
    m_hasScanned = true;
    m_playlistStore.load(m_library);
    m_playlistStore.pruneMissing(m_library);
    m_playlistStore.save();
    m_client.loadQueueTrackIds();
    clampSelectionForView();
    refreshAdaptivePalette();
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
        if (fresh.track_id != previousTrackId)
            refreshAdaptivePalette();

        const bool session = statusFlag(fresh, switchu::music::MusicStatus_SessionActive);
        if (session != m_lastSessionGuardState) {
            m_lastSessionGuardState = session;
            if (m_sessionGuardCb) m_sessionGuardCb(session);
            DebugLog::log("[music-diag] SESSION state=%d playing=%d paused=%d",
                          session ? 1 : 0,
                          statusFlag(fresh, switchu::music::MusicStatus_Playing) ? 1 : 0,
                          statusFlag(fresh, switchu::music::MusicStatus_Paused) ? 1 : 0);
        }
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
    finishAdaptivePaletteIfReady();
    // Smooth colour morph between album palettes instead of a hard background
    // cut when the asynchronous cover analysis completes.
    const float paletteBlend = 1.f - std::exp(-std::max(0.f, dt) * 5.2f);
    m_paletteAccent.r += (m_paletteTargetAccent.r - m_paletteAccent.r) * paletteBlend;
    m_paletteAccent.g += (m_paletteTargetAccent.g - m_paletteAccent.g) * paletteBlend;
    m_paletteAccent.b += (m_paletteTargetAccent.b - m_paletteAccent.b) * paletteBlend;
    m_paletteSoft = {
        0.90f + m_paletteAccent.r * 0.10f,
        0.90f + m_paletteAccent.g * 0.10f,
        0.90f + m_paletteAccent.b * 0.10f,
        1.f
    };
    gMusicAccent = m_paletteAccent;

    if (!m_active) {
        m_hiddenGuardTimer += dt;
        if (m_hiddenCoverReleaseTimer > 0.f) {
            m_hiddenCoverReleaseTimer = std::max(0.f, m_hiddenCoverReleaseTimer - dt);
            if (m_hiddenCoverReleaseTimer == 0.f && m_coverCache.size() > 0) {
                DebugLog::log("[music-diag] UNLOAD_COVER deferred cache=%zu", m_coverCache.size());
                m_coverCache.clear();
            }
        }
        if (m_hiddenGuardTimer >= 0.50f) {
            m_hiddenGuardTimer = 0.f;
            refreshStatus(true);
            std::error_code ec;
            const bool active = std::filesystem::exists(switchu::music::kSessionFlagPath, ec);
            if (active != m_lastSessionGuardState) {
                m_lastSessionGuardState = active;
                if (m_sessionGuardCb) m_sessionGuardCb(active);
            }
        }
        return;
    }

    m_hiddenGuardTimer = 0.f;
    m_transitionAlpha = std::min(1.f, m_transitionAlpha + dt / 0.30f);
    if (m_closing) {
        m_transitionAlpha = std::max(0.f, m_transitionAlpha - dt / 0.22f);
        if (m_transitionAlpha <= 0.f) {
            m_closing = false;
            if (m_closeCb) m_closeCb(); else hide();
            return;
        }
    }

    finishScanIfReady();
    refreshStatus(false);
    updateBattery(dt);
}


void MusicScreen::setTab(int index) {
    index = (index % kTabCount + kTabCount) % kTabCount;
    m_tabIndex = index;
    m_selection = 0;
    m_homeSection = 0;
    m_view = index == 0 ? View::Albums : View::Playlists;
    refreshAdaptivePalette();
}


void MusicScreen::clampSelectionForView() {
    size_t count = 0;
    switch (m_view) {
        case View::Albums: count = m_library.albums.size(); break;
        case View::Artists: count = m_library.artists.size(); break;
        case View::Tracks: count = m_library.tracks.size(); break;
        case View::Playlists: count = m_playlistStore.playlists().size(); break;
        case View::AlbumDetail:
            if (m_detailAlbum < m_library.albums.size()) count = m_library.albums[m_detailAlbum].tracks.size();
            break;
        case View::ArtistDetail:
            if (m_detailArtist < m_library.artists.size()) count = m_library.artists[m_detailArtist].tracks.size();
            break;
        case View::PlaylistDetail:
            if (m_detailPlaylist < m_playlistStore.playlists().size()) count = m_playlistStore.playlists()[m_detailPlaylist].trackIds.size();
            break;
        case View::Queue: count = m_client.queueTrackIds().size(); break;
        case View::SearchResults: count = m_searchResults.size(); break;
        default: return;
    }
    if (count == 0) m_selection = 0;
    else m_selection = std::clamp(m_selection, 0, static_cast<int>(count) - 1);
}

void MusicScreen::moveSelection(int dx, int dy) {
    if (m_view == View::Albums || m_view == View::Playlists) {
        if (dx != 0) {
            m_selection += dx;
            clampSelectionForView();
            refreshAdaptivePalette();
        }
        return;
    }

    if (m_view == View::NowPlaying) {
        if (dx != 0) updateNowPlayingSelection(dx);
        else if (dy != 0) {
            const float v = std::clamp(m_status.volume - dy * 0.05f, 0.f, 1.f);
            m_client.setVolume(v);
            m_status.volume = v;
        }
        return;
    }

    m_selection += dy != 0 ? dy : dx;
    clampSelectionForView();
}


void MusicScreen::openAlbum(size_t index) {
    if (index >= m_library.albums.size()) return;
    DebugLog::log("[music-diag] OPEN_ALBUM index=%zu title=%s", index, m_library.albums[index].title.c_str());
    m_returnView = View::Albums;
    m_detailAlbum = index;
    m_view = View::AlbumDetail;
    m_selection = 0;
    refreshAdaptivePalette();
}


void MusicScreen::openArtist(size_t index) {
    if (index >= m_library.artists.size()) return;
    m_returnView = m_view;
    m_detailArtist = index;
    m_view = View::ArtistDetail;
    m_selection = 0;
}

void MusicScreen::openPlaylist(size_t index) {
    if (index >= m_playlistStore.playlists().size()) return;
    DebugLog::log("[music-diag] OPEN_PLAYLIST index=%zu name=%s", index, m_playlistStore.playlists()[index].name.c_str());
    m_returnView = View::Playlists;
    m_detailPlaylist = index;
    m_view = View::PlaylistDetail;
    m_selection = 0;
    refreshAdaptivePalette();
}


std::vector<uint64_t> MusicScreen::albumTrackIds(size_t albumIndex) const {
    std::vector<uint64_t> ids;
    if (albumIndex >= m_library.albums.size()) return ids;
    for (size_t ti : m_library.albums[albumIndex].tracks)
        if (ti < m_library.tracks.size()) ids.push_back(m_library.tracks[ti].id);
    return ids;
}

std::vector<uint64_t> MusicScreen::artistTrackIds(size_t artistIndex) const {
    std::vector<uint64_t> ids;
    if (artistIndex >= m_library.artists.size()) return ids;
    for (size_t ti : m_library.artists[artistIndex].tracks)
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
                  ids.size(), index, static_cast<unsigned long long>(ids[(size_t)index]));
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


void MusicScreen::playTrackIndices(const std::vector<size_t>& indices, int index) {
    std::vector<uint64_t> ids;
    ids.reserve(indices.size());
    for (size_t ti : indices)
        if (ti < m_library.tracks.size()) ids.push_back(m_library.tracks[ti].id);
    playTrackIds(ids, index);
}

void MusicScreen::activateSelection() {
    if (m_scanRunning) return;
    switch (m_view) {
        case View::Home:
            m_view = View::Albums;
            m_tabIndex = 0;
            break;
        case View::Albums:
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size())
                playTrackIds(albumTrackIds(static_cast<size_t>(m_selection)), 0);
            break;
        case View::Artists:
            openArtist(static_cast<size_t>(m_selection));
            break;
        case View::Tracks: {
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.tracks.size()) {
                std::vector<uint64_t> ids; ids.reserve(m_library.tracks.size());
                for (const auto& t : m_library.tracks) ids.push_back(t.id);
                playTrackIds(ids, m_selection);
            }
            break;
        }
        case View::Playlists:
            openPlaylist(static_cast<size_t>(m_selection));
            break;
        case View::AlbumDetail:
            if (m_detailAlbum < m_library.albums.size())
                playTrackIds(albumTrackIds(m_detailAlbum), m_selection);
            break;
        case View::ArtistDetail:
            if (m_detailArtist < m_library.artists.size())
                playTrackIds(artistTrackIds(m_detailArtist), m_selection);
            break;
        case View::PlaylistDetail:
            playTrackIds(playlistTrackIds(m_detailPlaylist), m_selection);
            break;
        case View::SearchResults: {
            std::vector<uint64_t> ids;
            for (size_t ti : m_searchResults)
                if (ti < m_library.tracks.size()) ids.push_back(m_library.tracks[ti].id);
            playTrackIds(ids, m_selection);
            break;
        }
        case View::Queue:
            if (!m_client.queueTrackIds().empty()) {
                DebugLog::log("[music-diag] QUEUE_PLAY index=%d", m_selection);
                m_client.playIndex(m_selection);
                refreshStatus(true);
            }
            break;
        case View::NowPlaying:
            activateNowPlayingControl();
            break;
    }
}


void MusicScreen::goBack() {
    switch (m_view) {
        case View::Home:
        case View::Albums:
        case View::Artists:
        case View::Tracks:
        case View::Playlists:
            if (!m_closing) {
                DebugLog::log("[music-diag] CLOSE_MUSIC requested view=%d", static_cast<int>(m_view));
                m_closing = true;
                setFocusable(false);
            }
            return;
        case View::NowPlaying:
        case View::Queue:
        case View::SearchResults:
        case View::AlbumDetail:
        case View::ArtistDetail:
        case View::PlaylistDetail:
            m_view = m_returnView;
            m_selection = 0;
            refreshAdaptivePalette();
            return;
    }
}


size_t MusicScreen::selectedTrackIndex() const {
    if (m_view == View::Tracks && m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.tracks.size())
        return static_cast<size_t>(m_selection);
    if (m_view == View::AlbumDetail && m_detailAlbum < m_library.albums.size()) {
        const auto& v = m_library.albums[m_detailAlbum].tracks;
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < v.size()) return v[static_cast<size_t>(m_selection)];
    }
    if (m_view == View::ArtistDetail && m_detailArtist < m_library.artists.size()) {
        const auto& v = m_library.artists[m_detailArtist].tracks;
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < v.size()) return v[static_cast<size_t>(m_selection)];
    }
    if (m_view == View::SearchResults && m_selection >= 0 && static_cast<size_t>(m_selection) < m_searchResults.size())
        return m_searchResults[static_cast<size_t>(m_selection)];
    return static_cast<size_t>(-1);
}

void MusicScreen::contextualX() {
    // V0.02 carousel secondary action: open the selected object without
    // starting playback. This matches the visual split between Lire/Ouvrir.
    if (m_view == View::Albums) {
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size())
            openAlbum(static_cast<size_t>(m_selection));
        return;
    }
    if (m_view == View::Playlists) {
        if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists().size())
            openPlaylist(static_cast<size_t>(m_selection));
        return;
    }
    if (m_view == View::PlaylistDetail) {
        if (m_detailPlaylist < m_playlistStore.playlists().size() &&
            m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists()[m_detailPlaylist].trackIds.size()) {
            DebugLog::log("[music-diag] PLAYLIST_REMOVE playlist=%zu row=%d", m_detailPlaylist, m_selection);
            m_playlistStore.removeTrack(m_detailPlaylist, static_cast<size_t>(m_selection));
            m_playlistStore.save();
            clampSelectionForView();
        }
        return;
    }
    const size_t ti = selectedTrackIndex();
    if (ti != static_cast<size_t>(-1) && ti < m_library.tracks.size()) {
        openPlaylistChooser(m_library.tracks[ti].id);
        return;
    }
    openSearch();
}


void MusicScreen::contextualY() {
    if (m_view == View::Albums && m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size()) {
        appendToQueue(albumTrackIds(static_cast<size_t>(m_selection)));
        return;
    }
    if (m_view == View::Playlists && m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists().size()) {
        appendToQueue(playlistTrackIds(static_cast<size_t>(m_selection)));
        return;
    }
    if (m_view == View::AlbumDetail) {
        appendToQueue(albumTrackIds(m_detailAlbum));
        return;
    }
    if (m_view == View::PlaylistDetail) {
        appendToQueue(playlistTrackIds(m_detailPlaylist));
        return;
    }
    if (!m_client.queueTrackIds().empty()) {
        m_returnView = m_view;
        m_view = View::Queue;
        m_selection = std::max(0, m_status.current_index);
        clampSelectionForView();
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

void MusicScreen::openSearch() {
    m_modal = Modal::SearchKeyboard;
    m_keyboardText.clear();
    m_keyboardIndex = 0;
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
    if (m_modal == Modal::SearchKeyboard) {
        m_searchQuery = m_keyboardText;
        m_searchResults = MusicLibrary::searchTracks(m_library, m_searchQuery);
        m_returnView = m_view;
        m_view = View::SearchResults;
        m_selection = 0;
        m_modal = Modal::None;
        return;
    }
    if (m_modal == Modal::PlaylistNameKeyboard) {
        std::string name = m_keyboardText;
        if (name.empty()) name = "Nouvelle playlist";
        const size_t created = m_playlistStore.create(name);
        if (m_pendingPlaylistTrackId != 0)
            m_playlistStore.addTrack(created, m_pendingPlaylistTrackId);
        m_pendingPlaylistTrackId = 0;
        m_playlistStore.save();
        m_tabIndex = 1;
        m_view = View::Playlists;
        m_selection = static_cast<int>(created);
        m_modal = Modal::None;
    }
}

void MusicScreen::modalCancel() {
    m_pendingPlaylistTrackId = 0;
    m_modal = Modal::None;
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
            (unsigned long long)(seconds / 3600),
            (unsigned long long)((seconds / 60) % 60),
            (unsigned long long)(seconds % 60));
    else
        std::snprintf(out, sizeof(out), "%llu:%02llu",
            (unsigned long long)(seconds / 60),
            (unsigned long long)(seconds % 60));
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

CoverRef MusicScreen::selectedCover() const {
    switch (m_view) {
        case View::Albums:
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.albums.size())
                return m_library.albums[static_cast<size_t>(m_selection)].cover;
            break;
        case View::Playlists:
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists().size())
                return playlistCover(static_cast<size_t>(m_selection));
            break;
        case View::AlbumDetail:
            if (m_detailAlbum < m_library.albums.size()) return m_library.albums[m_detailAlbum].cover;
            break;
        case View::PlaylistDetail:
            return playlistCover(m_detailPlaylist);
        case View::NowPlaying: {
            const Track* t = currentTrack();
            if (t) return t->cover;
            break;
        }
        default:
            break;
    }
    const Track* t = currentTrack();
    if (t && t->cover.valid()) return t->cover;
    if (!m_library.albums.empty()) return m_library.albums.front().cover;
    return {};
}

nxui::Color MusicScreen::extractCoverAccent(const CoverRef& cover) const {
    if (!cover.valid()) return {0.46f, 0.31f, 0.92f, 1.f};

    // Keep palette extraction intentionally bounded: it is visual metadata,
    // never a reason to allocate tens of MB for a giant embedded artwork.
    constexpr uint64_t kMaxPaletteBytes = 4ULL * 1024ULL * 1024ULL;
    std::vector<uint8_t> bytes;
    if (cover.embedded) {
        if (cover.size == 0 || cover.size > kMaxPaletteBytes)
            return {0.46f, 0.31f, 0.92f, 1.f};
        std::ifstream file(cover.path, std::ios::binary);
        if (!file.is_open()) return {0.46f, 0.31f, 0.92f, 1.f};
        file.seekg(static_cast<std::streamoff>(cover.offset), std::ios::beg);
        bytes.resize(static_cast<size_t>(cover.size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return {0.46f, 0.31f, 0.92f, 1.f};
    } else {
        std::error_code ec;
        const uint64_t size = std::filesystem::file_size(cover.path, ec);
        if (ec || size == 0 || size > kMaxPaletteBytes)
            return {0.46f, 0.31f, 0.92f, 1.f};
        std::ifstream file(cover.path, std::ios::binary);
        if (!file.is_open()) return {0.46f, 0.31f, 0.92f, 1.f};
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels) ||
        w <= 0 || h <= 0 || static_cast<uint64_t>(w) * static_cast<uint64_t>(h) > 2'000'000ULL)
        return {0.46f, 0.31f, 0.92f, 1.f};

    unsigned char* rgba = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (!rgba) return {0.46f, 0.31f, 0.92f, 1.f};

    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    const uint64_t step = std::max<uint64_t>(1, pixels / 5000ULL);
    double rr = 0.0, gg = 0.0, bb = 0.0, weightSum = 0.0;
    for (uint64_t i = 0; i < pixels; i += step) {
        const float r = rgba[i * 4 + 0] / 255.f;
        const float g = rgba[i * 4 + 1] / 255.f;
        const float b = rgba[i * 4 + 2] / 255.f;
        const float a = rgba[i * 4 + 3] / 255.f;
        if (a < 0.45f) continue;
        const float mx = std::max({r, g, b});
        const float mn = std::min({r, g, b});
        const float sat = mx - mn;
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (lum < 0.055f || lum > 0.96f) continue;
        const float weight = 0.18f + sat * 1.65f;
        rr += r * weight;
        gg += g * weight;
        bb += b * weight;
        weightSum += weight;
    }
    stbi_image_free(rgba);

    if (weightSum <= 0.001)
        return {0.46f, 0.31f, 0.92f, 1.f};

    float r = static_cast<float>(rr / weightSum);
    float g = static_cast<float>(gg / weightSum);
    float b = static_cast<float>(bb / weightSum);
    const float mean = (r + g + b) / 3.f;
    // Slight saturation boost keeps pastel backgrounds readable while making
    // the selected album visibly influence the environment.
    r = std::clamp(mean + (r - mean) * 1.30f, 0.12f, 0.88f);
    g = std::clamp(mean + (g - mean) * 1.30f, 0.12f, 0.88f);
    b = std::clamp(mean + (b - mean) * 1.30f, 0.12f, 0.88f);
    return {r, g, b, 1.f};
}

void MusicScreen::refreshAdaptivePalette() {
    const CoverRef cover = selectedCover();
    const std::string key = cover.valid() ? cover.key() : std::string("<neutral>");
    m_paletteRequestedKey = key;
    m_paletteRequestedCover = cover;

    if (key == "<neutral>") {
        m_paletteCoverKey = key;
        m_paletteTargetAccent = {0.46f, 0.31f, 0.92f, 1.f};
        return;
    }

    auto cached = m_paletteCache.find(key);
    if (cached != m_paletteCache.end()) {
        m_paletteCoverKey = key;
        m_paletteTargetAccent = cached->second;
        return;
    }

    // Never decode artwork synchronously in the carousel input path. V0.01
    // could stall for a large JPEG/PNG exactly while the user was scrolling.
    // One bounded worker at a time keeps navigation fluid and memory stable.
    if (!m_paletteRunning) {
        const CoverRef jobCover = cover;
        const std::string jobKey = key;
        DebugLog::log("[music-diag] PALETTE async begin cover=%s", jobCover.path.c_str());
        m_paletteRunning = true;
        m_paletteFuture = std::async(std::launch::async, [this, jobCover, jobKey]() {
            return PaletteJobResult{jobKey, extractCoverAccent(jobCover)};
        });
    }
}

void MusicScreen::finishAdaptivePaletteIfReady() {
    if (!m_paletteRunning || !m_paletteFuture.valid())
        return;
    if (m_paletteFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    PaletteJobResult result = m_paletteFuture.get();
    m_paletteRunning = false;
    m_paletteCache[result.key] = result.color;
    DebugLog::log("[music-diag] PALETTE async done key=%s", result.key.c_str());

    if (result.key == m_paletteRequestedKey) {
        m_paletteCoverKey = result.key;
        m_paletteTargetAccent = result.color;
        return;
    }

    // Selection changed while the old cover was being sampled. Start only the
    // latest requested palette rather than queueing every intermediate cover.
    auto cached = m_paletteCache.find(m_paletteRequestedKey);
    if (cached != m_paletteCache.end()) {
        m_paletteCoverKey = m_paletteRequestedKey;
        m_paletteTargetAccent = cached->second;
        return;
    }
    if (m_paletteRequestedKey != "<neutral>" && m_paletteRequestedCover.valid()) {
        const CoverRef jobCover = m_paletteRequestedCover;
        const std::string jobKey = m_paletteRequestedKey;
        m_paletteRunning = true;
        m_paletteFuture = std::async(std::launch::async, [this, jobCover, jobKey]() {
            return PaletteJobResult{jobKey, extractCoverAccent(jobCover)};
        });
    }
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
            const float alpha = (0.16f * std::pow(1.f - t0, 2.2f)) * gMusicUiAlpha;
            const nxui::Color tint{1.f, 1.f, 1.f, alpha};
            const nxui::Vec2 p0{r.x, y0}, p1{r.x + r.width, y0};
            const nxui::Vec2 p2{r.x + r.width, y1}, p3{r.x, y1};
            ren.drawTexturedTriangle(slot, p0, {0.f, vTop}, p1, {1.f, vTop}, p2, {1.f, vBottom}, tint);
            ren.drawTexturedTriangle(slot, p0, {0.f, vTop}, p2, {1.f, vBottom}, p3, {0.f, vBottom}, tint);
        }
    }
}

void MusicScreen::drawCrtBackground(nxui::Renderer& ren) {
    const float a = gMusicUiAlpha;
    const nxui::Color soft = m_paletteSoft;
    // V0.02 approved light music environment. The selected cover softly
    // influences large ambient fields while the center stays neutral/readable.
    ren.drawRect({0.f, 0.f, kScreenW, kScreenH}, {0.965f, 0.969f, 0.982f, a});
    ren.drawCircle({105.f, 385.f}, 365.f,
                   {m_paletteAccent.r, m_paletteAccent.g, m_paletteAccent.b, 0.10f * a}, 96);
    ren.drawCircle({1185.f, 175.f}, 330.f,
                   {soft.r, soft.g, soft.b, 0.48f * a}, 96);
    ren.drawCircle({655.f, -185.f}, 430.f,
                   {1.f, 1.f, 1.f, 0.62f * a}, 96);

    // SwitchU/CRT family: broad curved-looking translucent bands and extremely
    // subtle scan rhythm, intentionally much lighter than HOME.
    for (int i = 0; i < 6; ++i) {
        const float y = 70.f + i * 62.f;
        const float x = 350.f + i * 37.f;
        ren.drawRoundedRect({x, y, 960.f - i * 55.f, 19.f},
                            {0.74f, 0.80f, 0.90f, (0.055f - i * 0.004f) * a}, 10.f);
    }
    for (int y = 0; y < 720; y += 12)
        ren.drawRect({0.f, static_cast<float>(y), kScreenW, 1.f},
                     {0.22f, 0.26f, 0.33f, 0.012f * a});

    // Reflective floor replaces HOME's black lower band.
    ren.drawGradientRect({0.f, 490.f, kScreenW, 230.f},
                         {1.f, 1.f, 1.f, 0.02f * a},
                         {1.f, 1.f, 1.f, 0.66f * a});
    ren.drawRect({0.f, 612.f, kScreenW, 1.f}, {0.48f, 0.50f, 0.57f, 0.08f * a});
}


void MusicScreen::drawTopBar(nxui::Renderer& ren) {
    if (!m_font || !m_smallFont) return;

    // Time + simple profile marker match the HOME information rhythm.
    ren.drawText(formatClock(), {37.f, 26.f}, m_font, textPrimary(), 1.28f);
    const float px = 176.f, py = 47.f;
    if (m_profileTexture && m_profileTexture->valid()) {
        ren.drawTextureRounded(m_profileTexture, {px - 28.f, py - 28.f, 56.f, 56.f}, 28.f,
                               {1.f, 1.f, 1.f, gMusicUiAlpha});
        ren.drawRoundedRectOutline({px - 29.f, py - 29.f, 58.f, 58.f},
                                   {1.f,1.f,1.f,0.72f * gMusicUiAlpha}, 29.f, 1.4f);
    } else {
        ren.drawCircle({px, py}, 27.f, {0.42f, 0.30f, 0.88f, 0.98f * gMusicUiAlpha}, 52);
        ren.drawCircle({px - 6.f, py - 5.f}, 6.f, {1.f,1.f,1.f,0.90f * gMusicUiAlpha}, 32);
        ren.drawCircle({px + 7.f, py - 4.f}, 4.5f, {1.f,1.f,1.f,0.78f * gMusicUiAlpha}, 32);
    }

    // Approved Albums / Playlists music pill. This replaces the three-way
    // HOME pill only after the transition into the native Music environment.
    const nxui::Rect nav{443.f, 20.f, 394.f, 55.f};
    ren.drawRoundedRect(nav, {0.82f, 0.84f, 0.90f, 0.34f * gMusicUiAlpha}, 27.f);
    ren.drawRoundedRectOutline(nav, {1.f,1.f,1.f,0.84f * gMusicUiAlpha}, 27.f, 1.4f);
    const float cellW = nav.width * 0.5f;
    nxui::Rect activeRect{nav.x + (m_tabIndex == 0 ? 4.f : cellW), nav.y + 4.f,
                          cellW - 4.f, nav.height - 8.f};
    ren.drawRoundedRect(activeRect, {1.f, 1.f, 1.f, 0.88f * gMusicUiAlpha}, 23.f);
    ren.drawRoundedRectOutline(activeRect, {1.f,1.f,1.f,0.96f * gMusicUiAlpha}, 23.f, 1.f);

    for (int i = 0; i < kTabCount; ++i) {
        const std::string label = kTabs[i];
        const auto sz = m_smallFont->measure(label);
        const float cx = nav.x + cellW * (i + 0.5f);
        ren.drawText(label, {cx - sz.x * 0.5f, nav.y + 17.f}, m_smallFont,
                     i == m_tabIndex ? textPrimary() : textSecondary(0.86f), 1.0f);
    }

    if (m_iconFont) {
        const std::string lg = buttonGlyph(nxui::Button::L);
        const std::string rg = buttonGlyph(nxui::Button::R);
        ren.drawText(lg, {414.f, 34.f}, m_iconFont, textSecondary(0.86f), 0.82f);
        ren.drawText(rg, {853.f, 34.f}, m_iconFont, textSecondary(0.86f), 0.82f);
    }

    // Minimal Wi-Fi silhouette.
    const nxui::Color hud = textSecondary(0.92f);
    ren.drawLine({1072.f, 42.f}, {1084.f, 34.f}, hud, 3.f);
    ren.drawLine({1084.f, 34.f}, {1096.f, 42.f}, hud, 3.f);
    ren.drawLine({1077.f, 47.f}, {1084.f, 43.f}, hud, 3.f);
    ren.drawLine({1084.f, 43.f}, {1091.f, 47.f}, hud, 3.f);
    ren.drawCircle({1084.f, 52.f}, 2.4f, hud, 16);

    const float level = std::clamp(m_batteryPercent / 100.f, 0.f, 1.f);
    nxui::Rect body{1150.f, 32.f, 44.f, 21.f};
    ren.drawRoundedRectOutline(body, textSecondary(0.90f), 4.f, 1.6f);
    ren.drawRoundedRect({1196.f, 38.f, 4.f, 9.f}, textSecondary(0.78f), 1.6f);
    nxui::Color bc = level <= 0.20f ? nxui::Color{0.96f,0.20f,0.18f,0.96f * gMusicUiAlpha}
                    : level <= 0.50f ? nxui::Color{0.95f,0.68f,0.16f,0.96f * gMusicUiAlpha}
                    : accent(0.92f);
    if (m_batteryCharging) bc = {0.96f, 0.72f, 0.10f, 0.96f * gMusicUiAlpha};
    if (level > 0.01f)
        ren.drawRoundedRect({1153.f,35.f,38.f * level,15.f}, bc, 2.8f);
    ren.drawText(std::to_string(m_batteryPercent) + "%", {1208.f, 30.f}, m_font,
                 textSecondary(), 0.92f);
}


void MusicScreen::drawCover(nxui::Renderer& ren, const CoverRef& cover,
                            const nxui::Rect& r, bool selected, int maxSide) {
    const nxui::Texture* tex = m_coverCache.get(cover, ren, maxSide);
    if (tex) {
        ren.drawTextureRounded(tex, r, 14.f, {1.f,1.f,1.f,gMusicUiAlpha});
    } else {
        ren.drawRoundedRect(r, {0.90f, 0.91f, 0.94f, 0.96f * gMusicUiAlpha}, 14.f);
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
    ren.drawText(title, {78.f, 285.f}, m_font, textPrimary(), 1.50f);
    ren.drawText(detail, {80.f, 340.f}, m_smallFont, textSecondary(), 0.94f);
}


void MusicScreen::drawHome(nxui::Renderer& ren) {
    if (!m_font || !m_smallFont) return;
    float y = kContentTop;
    const Track* cur = currentTrack();
    if (cur) {
        const bool sel = m_homeSection == 0;
        ren.drawText("Reprendre l’écoute", {48.f, y}, m_font, textPrimary(), 0.94f);
        nxui::Rect hero{48.f, y + 30.f, 1184.f, 112.f};
        ren.drawRoundedRect(hero, sel ? panel2(0.96f) : panel(0.90f), 20.f);
        drawCover(ren, cur->cover, {hero.x + 14.f, hero.y + 10.f, 92.f, 92.f}, false, 160);
        ren.drawText(cur->title, {hero.x + 128.f, hero.y + 19.f}, m_font, textPrimary(), 1.02f);
        ren.drawText(cur->artist + " · " + cur->album, {hero.x + 128.f, hero.y + 55.f},
                     m_smallFont, textSecondary(), 0.80f);
        const float prog = m_status.duration_ms > 0
            ? std::clamp(float(double(m_status.position_ms) / double(m_status.duration_ms)), 0.f, 1.f) : 0.f;
        ren.drawRoundedRect({hero.x + 128.f, hero.y + 83.f, 650.f, 5.f}, {0.18f,0.20f,0.23f,1.f}, 2.5f);
        ren.drawRoundedRect({hero.x + 128.f, hero.y + 83.f, 650.f * prog, 5.f}, accent(), 2.5f);
        ren.drawText(formatDuration(m_status.position_ms) + " / " + formatDuration(m_status.duration_ms),
                     {hero.x + 808.f, hero.y + 76.f}, m_smallFont, textSecondary(), 0.70f);
        y += 158.f;
    }

    auto drawAlbumStrip = [&](const char* heading, const std::vector<size_t>& albumIndices,
                              int section, float sy, int maxItems) {
        ren.drawText(heading, {48.f, sy}, m_font, textPrimary(), 0.86f);
        const size_t count = std::min<size_t>(static_cast<size_t>(maxItems), albumIndices.size());
        for (size_t i = 0; i < count; ++i) {
            const size_t ai = albumIndices[i];
            if (ai >= m_library.albums.size()) continue;
            const auto& a = m_library.albums[ai];
            const float x = 48.f + i * 176.f;
            drawCover(ren, a.cover, {x, sy + 27.f, 100.f, 100.f},
                      m_homeSection == section && m_selection == (int)i, 160);
            ren.drawText(a.title, {x, sy + 132.f}, m_smallFont, textPrimary(), 0.66f);
            ren.drawText(a.artist, {x, sy + 151.f}, m_smallFont, textSecondary(), 0.57f);
        }
    };

    std::vector<size_t> recent;
    for (size_t i = 0; i < std::min<size_t>(5, m_library.recentAlbums.size()); ++i)
        recent.push_back(m_library.recentAlbums[i]);
    drawAlbumStrip("Ajoutés récemment", recent, 1, y, 5);

    const float lowerY = y + 174.f;
    std::vector<size_t> firstAlbums;
    for (size_t i = 0; i < std::min<size_t>(5, m_library.albums.size()); ++i)
        firstAlbums.push_back(i);
    drawAlbumStrip("Vos albums", firstAlbums, 2, lowerY, 5);

    const auto& playlists = m_playlistStore.playlists();
    if (!playlists.empty()) {
        ren.drawText("Playlists", {938.f, lowerY}, m_font, textPrimary(), 0.86f);
        for (size_t i = 0; i < std::min<size_t>(3, playlists.size()); ++i) {
            nxui::Rect pr{938.f, lowerY + 30.f + i * 42.f, 285.f, 35.f};
            ren.drawRoundedRect(pr,
                m_homeSection == 3 && m_selection == (int)i ? panel2() : panel(0.85f), 10.f);
            ren.drawText(playlists[i].name, {pr.x + 12.f, pr.y + 9.f},
                         m_smallFont, textPrimary(), 0.64f);
        }
    }
}

void MusicScreen::drawAlbums(nxui::Renderer& ren) {
    if (m_library.albums.empty()) {
        drawEmpty(ren, "Aucun album", "Ajoute des fichiers MP3 ou FLAC dans sdmc:/Music/.");
        return;
    }
    m_selection = std::clamp(m_selection, 0, static_cast<int>(m_library.albums.size()) - 1);

    struct Slot { int delta; float cx; float y; float size; float alpha; };
    const std::array<Slot,5> slots = {{
        {-2, 105.f, 274.f, 188.f, 0.58f},
        {-1, 350.f, 254.f, 218.f, 0.82f},
        { 0, 640.f, 178.f, 318.f, 1.00f},
        { 1, 930.f, 254.f, 218.f, 0.82f},
        { 2,1175.f, 274.f, 188.f, 0.58f},
    }};

    for (const auto& slot : slots) {
        const int idx = m_selection + slot.delta;
        if (idx < 0 || idx >= static_cast<int>(m_library.albums.size())) continue;
        const auto& album = m_library.albums[(size_t)idx];
        const nxui::Rect r{slot.cx - slot.size * 0.5f, slot.y, slot.size, slot.size};
        const float saved = gMusicUiAlpha;
        gMusicUiAlpha *= slot.alpha;
        drawReflectedCover(ren, album.cover, r, slot.delta == 0, slot.delta == 0 ? 512 : 320);
        gMusicUiAlpha = saved;
    }

    const auto& a = m_library.albums[(size_t)m_selection];
    const auto titleSize = m_font->measure(a.title);
    ren.drawText(a.title, {640.f - titleSize.x * 0.68f, 527.f}, m_font, textPrimary(), 1.36f);
    const auto artistSize = m_smallFont->measure(a.artist);
    ren.drawText(a.artist, {640.f - artistSize.x * 0.47f, 575.f}, m_smallFont, textSecondary(), 0.94f);

    ren.drawRect({405.f, 611.f, 470.f, 1.f}, {0.26f,0.28f,0.34f,0.28f * gMusicUiAlpha});
    const std::string info = formatDuration(albumDurationMs((size_t)m_selection));
    ren.drawText("◷  " + info, {438.f, 628.f}, m_smallFont, textSecondary(), 0.82f);
    if (m_iconFont) {
        ren.drawText(buttonGlyph(nxui::Button::A), {592.f, 626.f}, m_iconFont, textPrimary(), 0.78f);
        ren.drawText(buttonGlyph(nxui::Button::X), {720.f, 626.f}, m_iconFont, textPrimary(), 0.78f);
        ren.drawText(buttonGlyph(nxui::Button::Y), {842.f, 626.f}, m_iconFont, textPrimary(), 0.78f);
    }
    ren.drawText("Lire", {621.f, 630.f}, m_smallFont, textSecondary(), 0.78f);
    ren.drawText("Ouvrir", {749.f, 630.f}, m_smallFont, textSecondary(), 0.78f);
    ren.drawText("File", {871.f, 630.f}, m_smallFont, textSecondary(), 0.78f);

    ren.drawCircle({71.f, 650.f}, 34.f, {1.f,1.f,1.f,0.72f * gMusicUiAlpha}, 48);
    ren.drawText("⚙", {52.f, 629.f}, m_font, textSecondary(0.9f), 1.02f);
    ren.drawCircle({1210.f, 650.f}, 34.f, {1.f,1.f,1.f,0.72f * gMusicUiAlpha}, 48);
    ren.drawText("♪", {1192.f, 628.f}, m_font, textPrimary(), 1.12f);
}


int MusicScreen::visibleListStart(size_t count, int rows) const {
    if (count <= static_cast<size_t>(rows)) return 0;
    int start = m_selection - rows / 2;
    return std::clamp(start, 0, static_cast<int>(count) - rows);
}

void MusicScreen::drawTrackRow(nxui::Renderer& ren, const Track& t, const nxui::Rect& row,
                               bool selected, int ordinal) {
    if (selected) {
        ren.drawRoundedRect(row, {0.965f,0.955f,1.00f,0.96f * gMusicUiAlpha}, 14.f);
        ren.drawRoundedRectOutline(row, accent(0.32f), 14.f, 1.4f);
    }
    if (ordinal > 0) {
        char num[12]{}; std::snprintf(num, sizeof(num), "%d", ordinal);
        ren.drawText(num, {row.x + 17.f, row.y + 15.f}, m_smallFont,
                     selected ? accent() : textSecondary(), 0.82f);
    }
    ren.drawText(t.title, {row.x + 66.f, row.y + 12.f}, m_smallFont,
                 selected ? accent() : textPrimary(), 0.92f);
    ren.drawText(formatDuration(t.durationMs), {row.x + row.width - 72.f, row.y + 14.f},
                 m_smallFont, textSecondary(), 0.78f);
}


void MusicScreen::drawTracks(nxui::Renderer& ren, const std::vector<size_t>* overrideTracks,
                             const std::string& heading) {
    const size_t count = overrideTracks ? overrideTracks->size() : m_library.tracks.size();
    if (!heading.empty()) ren.drawText(heading, {55.f, 103.f}, m_font, textPrimary(), 1.02f);
    if (count == 0) {
        drawEmpty(ren, heading.empty() ? "Aucun morceau" : heading,
                  "La bibliothèque locale ne contient aucun morceau correspondant.");
        return;
    }
    const int start = visibleListStart(count, kListRows);
    for (int i = 0; i < kListRows; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(count)) break;
        const size_t ti = overrideTracks ? (*overrideTracks)[(size_t)idx] : static_cast<size_t>(idx);
        if (ti >= m_library.tracks.size()) continue;
        drawTrackRow(ren, m_library.tracks[ti], {70.f, 137.f + i * 56.f, 1140.f, 51.f},
                     idx == m_selection, idx + 1);
    }
}

void MusicScreen::drawArtists(nxui::Renderer& ren) {
    if (m_library.artists.empty()) {
        drawEmpty(ren, "Aucun artiste", "Les artistes sont reconstruits depuis les métadonnées locales.");
        return;
    }
    const int start = visibleListStart(m_library.artists.size(), kListRows);
    for (int i = 0; i < kListRows; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(m_library.artists.size())) break;
        const auto& a = m_library.artists[(size_t)idx];
        nxui::Rect row{70.f, 130.f + i * 58.f, 1140.f, 52.f};
        if (idx == m_selection) ren.drawRoundedRect(row, panel2(), 12.f);
        ren.drawCircle({row.x + 26.f, row.y + 26.f}, 18.f, {0.10f,0.13f,0.16f,1.f}, 36);
        ren.drawText(a.name, {row.x + 58.f, row.y + 12.f}, m_font, textPrimary(), 0.82f);
        ren.drawText(std::to_string(a.albums.size()) + " albums · " + std::to_string(a.tracks.size()) + " morceaux",
                     {row.x + 670.f, row.y + 16.f}, m_smallFont, textSecondary(), 0.70f);
    }
}

void MusicScreen::drawPlaylists(nxui::Renderer& ren) {
    const auto& ps = m_playlistStore.playlists();
    if (ps.empty()) {
        drawEmpty(ren, "Aucune playlist", "Appuie sur + pour créer ta première playlist locale.");
        return;
    }
    m_selection = std::clamp(m_selection, 0, static_cast<int>(ps.size()) - 1);

    struct Slot { int delta; float cx; float y; float size; float alpha; };
    const std::array<Slot,3> slots = {{
        {-1, 300.f, 222.f, 286.f, 0.62f},
        { 0, 640.f, 142.f, 380.f, 1.00f},
        { 1, 980.f, 222.f, 286.f, 0.62f},
    }};
    for (const auto& slot : slots) {
        const int idx = m_selection + slot.delta;
        if (idx < 0 || idx >= static_cast<int>(ps.size())) continue;
        const CoverRef cover = playlistCover((size_t)idx);
        const nxui::Rect r{slot.cx - slot.size * 0.5f, slot.y, slot.size, slot.size};
        const float saved = gMusicUiAlpha;
        gMusicUiAlpha *= slot.alpha;
        drawReflectedCover(ren, cover, r, slot.delta == 0, slot.delta == 0 ? 512 : 384);
        gMusicUiAlpha = saved;
    }

    const auto& pl = ps[(size_t)m_selection];
    const auto titleSize = m_font->measure(pl.name);
    ren.drawText(pl.name, {640.f - titleSize.x * 0.69f, 540.f}, m_font, textPrimary(), 1.38f);
    const std::string sub = "Playlist  •  " + std::to_string(pl.trackIds.size()) + " morceaux";
    const auto subSize = m_smallFont->measure(sub);
    ren.drawText(sub, {640.f - subSize.x * 0.46f, 587.f}, m_smallFont, textSecondary(), 0.92f);
    ren.drawRect({405.f, 621.f, 470.f, 1.f}, {0.26f,0.28f,0.34f,0.26f * gMusicUiAlpha});
    ren.drawText("◷  " + formatDuration(playlistDurationMs((size_t)m_selection)),
                 {446.f, 638.f}, m_smallFont, textSecondary(), 0.80f);
    if (m_iconFont) {
        ren.drawText(buttonGlyph(nxui::Button::A), {616.f, 635.f}, m_iconFont, textPrimary(), 0.78f);
        ren.drawText(buttonGlyph(nxui::Button::Y), {765.f, 635.f}, m_iconFont, textPrimary(), 0.78f);
    }
    ren.drawText("Ouvrir", {646.f, 639.f}, m_smallFont, textSecondary(), 0.78f);
    ren.drawText("Ajouter à la file", {795.f, 639.f}, m_smallFont, textSecondary(), 0.78f);
}


void MusicScreen::drawAlbumDetail(nxui::Renderer& ren) {
    if (m_detailAlbum >= m_library.albums.size()) return;
    const auto& a = m_library.albums[m_detailAlbum];

    drawReflectedCover(ren, a.cover, {112.f, 116.f, 350.f, 350.f}, false, 512);
    uint64_t totalMs = albumDurationMs(m_detailAlbum);
    std::string info = formatDuration(totalMs) + "  •  " + std::to_string(a.tracks.size()) + " morceaux";
    if (!a.genre.empty()) info += "  •  " + a.genre;
    if (a.year > 0) info += "  •  " + std::to_string(a.year);
    ren.drawText(info, {107.f, 525.f}, m_smallFont, textSecondary(), 0.76f);

    ren.drawRect({528.f, 104.f, 1.f, 492.f}, {0.26f,0.28f,0.34f,0.22f * gMusicUiAlpha});
    ren.drawText(a.title, {572.f, 102.f}, m_font, textPrimary(), 1.58f);
    ren.drawText(a.artist, {574.f, 160.f}, m_font, accent(), 0.92f);

    nxui::Rect listPanel{558.f, 208.f, 650.f, 382.f};
    ren.drawRoundedRect(listPanel, {1.f,1.f,1.f,0.58f * gMusicUiAlpha}, 22.f);
    ren.drawRoundedRectOutline(listPanel, {0.74f,0.76f,0.82f,0.26f * gMusicUiAlpha}, 22.f, 1.f);
    const int start = visibleListStart(a.tracks.size(), 7);
    for (int i = 0; i < 7; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(a.tracks.size())) break;
        const size_t ti = a.tracks[(size_t)idx];
        if (ti >= m_library.tracks.size()) continue;
        const Track& t = m_library.tracks[ti];
        const int ordinal = t.trackNumber > 0 ? t.trackNumber : idx + 1;
        drawTrackRow(ren, t, {568.f, 218.f + i * 51.f, 630.f, 46.f}, idx == m_selection, ordinal);
        if (i < 6) ren.drawRect({582.f, 267.f + i * 51.f, 600.f, 1.f},
                                {0.34f,0.35f,0.39f,0.10f * gMusicUiAlpha});
    }
}


void MusicScreen::drawArtistDetail(nxui::Renderer& ren) {
    if (m_detailArtist >= m_library.artists.size()) return;
    const auto& a = m_library.artists[m_detailArtist];
    ren.drawText(a.name, {65.f, 120.f}, m_font, textPrimary(), 1.35f);
    ren.drawText(std::to_string(a.albums.size()) + " albums · " + std::to_string(a.tracks.size()) + " morceaux",
                 {65.f, 167.f}, m_smallFont, textSecondary(), 0.82f);
    float chipX = 65.f;
    for (size_t i = 0; i < std::min<size_t>(4, a.albums.size()); ++i) {
        const size_t ai = a.albums[i];
        if (ai >= m_library.albums.size()) continue;
        const auto& album = m_library.albums[ai];
        drawCover(ren, album.cover, {chipX, 205.f, 92.f, 92.f}, false, 128);
        ren.drawText(album.title, {chipX, 301.f}, m_smallFont, textPrimary(), 0.63f);
        chipX += 145.f;
    }
    const int start = visibleListStart(a.tracks.size(), 5);
    for (int i = 0; i < 5; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(a.tracks.size())) break;
        const size_t ti = a.tracks[(size_t)idx];
        if (ti < m_library.tracks.size())
            drawTrackRow(ren, m_library.tracks[ti], {65.f, 350.f + i * 53.f, 1140.f, 48.f}, idx == m_selection, idx + 1);
    }
}

void MusicScreen::drawPlaylistDetail(nxui::Renderer& ren) {
    if (m_detailPlaylist >= m_playlistStore.playlists().size()) return;
    const auto& p = m_playlistStore.playlists()[m_detailPlaylist];
    const CoverRef cover = playlistCover(m_detailPlaylist);
    drawReflectedCover(ren, cover, {112.f, 116.f, 350.f, 350.f}, false, 512);
    ren.drawText(formatDuration(playlistDurationMs(m_detailPlaylist)) + "  •  " +
                 std::to_string(p.trackIds.size()) + " morceaux  •  Playlist",
                 {107.f, 525.f}, m_smallFont, textSecondary(), 0.78f);

    ren.drawRect({528.f, 104.f, 1.f, 492.f}, {0.26f,0.28f,0.34f,0.22f * gMusicUiAlpha});
    ren.drawText(p.name, {572.f, 102.f}, m_font, textPrimary(), 1.58f);
    ren.drawText("Playlist locale", {574.f, 160.f}, m_font, accent(), 0.92f);

    nxui::Rect listPanel{558.f, 208.f, 650.f, 382.f};
    ren.drawRoundedRect(listPanel, {1.f,1.f,1.f,0.58f * gMusicUiAlpha}, 22.f);
    ren.drawRoundedRectOutline(listPanel, {0.74f,0.76f,0.82f,0.26f * gMusicUiAlpha}, 22.f, 1.f);
    const int start = visibleListStart(p.trackIds.size(), 7);
    for (int i = 0; i < 7; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(p.trackIds.size())) break;
        const Track* t = trackForId(p.trackIds[(size_t)idx]);
        if (t) drawTrackRow(ren, *t, {568.f, 218.f + i * 51.f, 630.f, 46.f}, idx == m_selection, idx + 1);
    }
}


void MusicScreen::drawNowPlaying(nxui::Renderer& ren) {
    const Track* t = currentTrack();
    if (!t) {
        drawEmpty(ren, "Aucune lecture en cours", "Choisis un morceau dans ta bibliothèque.");
        return;
    }
    drawReflectedCover(ren, t->cover, {92.f, 142.f, 385.f, 385.f}, false, 512);
    ren.drawText("À l’écoute", {548.f, 132.f}, m_smallFont, textSecondary(), 0.86f);
    ren.drawText(t->title, {546.f, 181.f}, m_font, textPrimary(), 1.55f);
    ren.drawText(t->artist, {548.f, 238.f}, m_font, accent(), 1.00f);
    ren.drawText(t->album, {548.f, 279.f}, m_smallFont, textSecondary(), 0.88f);

    const float progress = m_status.duration_ms > 0
        ? std::clamp(float(double(m_status.position_ms) / double(m_status.duration_ms)), 0.f, 1.f) : 0.f;
    ren.drawRoundedRect({548.f, 348.f, 610.f, 8.f}, {0.72f,0.74f,0.79f,0.46f * gMusicUiAlpha}, 4.f);
    ren.drawRoundedRect({548.f, 348.f, 610.f * progress, 8.f}, accent(), 4.f);
    ren.drawText(formatDuration(m_status.position_ms), {548.f, 368.f}, m_smallFont, textSecondary(), 0.76f);
    ren.drawText(formatDuration(m_status.duration_ms), {1102.f, 368.f}, m_smallFont, textSecondary(), 0.76f);

    const std::array<std::string,5> labels = {
        statusFlag(m_status, switchu::music::MusicStatus_Shuffle) ? "⇄" : "↝",
        "◀◀",
        statusFlag(m_status, switchu::music::MusicStatus_Playing) ? "Ⅱ" : "▶",
        "▶▶",
        repeatLabel(m_status.repeat_mode)
    };
    for (int i = 0; i < 5; ++i) {
        const float x = 575.f + i * 116.f;
        nxui::Rect b{x, 418.f, 74.f, 62.f};
        if (i == m_nowControl) {
            ren.drawRoundedRect(b, {1.f,1.f,1.f,0.82f * gMusicUiAlpha}, 23.f);
            ren.drawRoundedRectOutline(b, accent(0.34f), 23.f, 1.5f);
        }
        ren.drawText(labels[(size_t)i], {b.x + 19.f, b.y + 14.f}, m_font,
                     i == m_nowControl ? accent() : textPrimary(), 0.96f);
    }
    ren.drawText("Volume " + std::to_string(int(std::round(m_status.volume * 100.f))) + "%",
                 {548.f, 514.f}, m_smallFont, textSecondary(), 0.80f);
    ren.drawRoundedRect({650.f, 522.f, 430.f, 6.f}, {0.72f,0.74f,0.79f,0.46f * gMusicUiAlpha}, 3.f);
    ren.drawRoundedRect({650.f, 522.f, 430.f * std::clamp(m_status.volume,0.f,1.f), 6.f}, accent(), 3.f);
}


void MusicScreen::drawQueue(nxui::Renderer& ren) {
    ren.drawText("À suivre", {65.f, 105.f}, m_font, textPrimary(), 1.15f);
    const auto& ids = m_client.queueTrackIds();
    if (ids.empty()) {
        drawEmpty(ren, "File d’attente vide", "Lance un album, un artiste ou une playlist.");
        return;
    }
    const int start = visibleListStart(ids.size(), 8);
    for (int i = 0; i < 8; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(ids.size())) break;
        const Track* t = trackForId(ids[(size_t)idx]);
        if (!t) continue;
        nxui::Rect r{70.f, 142.f + i * 54.f, 1140.f, 49.f};
        drawTrackRow(ren, *t, r, idx == m_selection, idx + 1);
        if (idx == m_status.current_index)
            ren.drawRoundedRect({r.x + 3.f, r.y + 6.f, 4.f, r.height - 12.f}, accent(), 2.f);
    }
}

void MusicScreen::drawMiniPlayer(nxui::Renderer& ren) {
    // V0.02 full Music screens use their own bottom action zones. The compact
    // player is now reserved for the global HOME module when Music is hidden.
    (void)ren;
}


void MusicScreen::drawHomeMiniModule(nxui::Renderer& ren) {
    const Track* t = currentTrack();
    if (!t || !hasMusicSession()) return;
    const nxui::Rect bar{34.f, 96.f, 430.f, 64.f};
    ren.drawRoundedRect(bar, {0.055f,0.060f,0.075f,0.90f}, 22.f);
    ren.drawRoundedRectOutline(bar, {1.f,1.f,1.f,0.15f}, 22.f, 1.f);
    const nxui::Texture* tex = m_coverCache.get(t->cover, ren, 96);
    if (tex) ren.drawTextureRounded(tex, {bar.x + 8.f, bar.y + 8.f, 48.f, 48.f}, 11.f);
    ren.drawText(t->title, {bar.x + 70.f, bar.y + 9.f}, m_smallFont,
                 {1.f,1.f,1.f,0.96f}, 0.78f);
    ren.drawText(t->artist, {bar.x + 70.f, bar.y + 34.f}, m_smallFont,
                 {0.74f,0.77f,0.83f,0.95f}, 0.66f);
    ren.drawText(statusFlag(m_status, switchu::music::MusicStatus_Playing) ? "Ⅱ" : "▶",
                 {bar.x + 338.f, bar.y + 15.f}, m_font, {1.f,1.f,1.f,0.96f}, 0.88f);
    ren.drawText("▶▶", {bar.x + 384.f, bar.y + 17.f}, m_font, {1.f,1.f,1.f,0.90f}, 0.72f);
}

void MusicScreen::drawBottomHints(nxui::Renderer& ren) {
    if (!m_smallFont || m_view == View::Albums || m_view == View::Playlists) return;
    std::vector<std::pair<nxui::Button,std::string>> hints;
    if (m_modal == Modal::SearchKeyboard || m_modal == Modal::PlaylistNameKeyboard) {
        hints = {{nxui::Button::A,"Saisir"},{nxui::Button::X,"Effacer"},{nxui::Button::Plus,"Valider"},{nxui::Button::B,"Annuler"}};
    } else if (m_modal == Modal::PlaylistChooser) {
        hints = {{nxui::Button::A,"Ajouter"},{nxui::Button::B,"Annuler"}};
    } else if (m_view == View::NowPlaying) {
        hints = {{nxui::Button::A,"Action"},{nxui::Button::ZL,"-10 s"},{nxui::Button::ZR,"+10 s"},{nxui::Button::Y,"File"},{nxui::Button::B,"Retour"}};
    } else {
        hints = {{nxui::Button::A,"Lire"},{nxui::Button::X,"Action"},{nxui::Button::Y,"File"},{nxui::Button::B,"Retour"}};
    }
    float x = 45.f;
    for (const auto& h : hints) {
        if (m_iconFont) {
            ren.drawText(buttonGlyph(h.first), {x, kBottomY}, m_iconFont, textPrimary(), 0.72f);
            x += 29.f;
        }
        ren.drawText(h.second, {x, kBottomY + 1.f}, m_smallFont, textSecondary(), 0.68f);
        x += std::max(102.f, m_smallFont->measure(h.second).x * 0.68f + 28.f);
    }
}


void MusicScreen::drawModal(nxui::Renderer& ren) {
    if (m_modal == Modal::None || !m_font || !m_smallFont) return;
    ren.drawRect({0,0,kScreenW,kScreenH}, {0.12f,0.14f,0.19f,0.24f * gMusicUiAlpha});
    nxui::Rect p{180.f, 115.f, 920.f, 480.f};
    ren.drawRoundedRect(p, {0.985f,0.988f,0.996f,0.985f * gMusicUiAlpha}, 26.f);
    ren.drawRoundedRectOutline(p, {0.58f,0.60f,0.68f,0.24f * gMusicUiAlpha}, 26.f, 1.4f);

    if (m_modal == Modal::PlaylistChooser) {
        ren.drawText("Ajouter à une playlist", {220.f, 148.f}, m_font, textPrimary(), 1.08f);
        const auto& ps = m_playlistStore.playlists();
        for (size_t i = 0; i < std::min<size_t>(7, ps.size()); ++i) {
            nxui::Rect r{225.f, 205.f + i * 50.f, 830.f, 43.f};
            if ((int)i == m_modalSelection) ren.drawRoundedRect(r, panel2(), 11.f);
            ren.drawText(ps[i].name, {r.x + 14.f,r.y + 11.f}, m_smallFont, textPrimary(), 0.76f);
        }
        return;
    }

    const char* title = m_modal == Modal::SearchKeyboard ? "Rechercher dans la bibliothèque" : "Nom de la playlist";
    ren.drawText(title, {220.f, 148.f}, m_font, textPrimary(), 1.04f);
    ren.drawRoundedRect({220.f, 191.f, 840.f, 52.f}, {0.93f,0.94f,0.97f,0.96f * gMusicUiAlpha}, 13.f);
    ren.drawText(m_keyboardText.empty() ? "…" : m_keyboardText, {238.f,205.f}, m_font,
                 m_keyboardText.empty() ? textSecondary() : textPrimary(), 0.86f);

    static const std::array<std::string,42> keys = {
        "A","B","C","D","E","F","G","H","I","J",
        "K","L","M","N","O","P","Q","R","S","T",
        "U","V","W","X","Y","Z","0","1","2","3",
        "4","5","6","7","8","9","Espace","-","_","'",".","←"
    };
    constexpr int cols = 10;
    for (int i = 0; i < (int)keys.size(); ++i) {
        const int col = i % cols, row = i / cols;
        nxui::Rect r{225.f + col * 82.f, 270.f + row * 58.f, 72.f, 48.f};
        if (i == m_keyboardIndex) ren.drawRoundedRect(r, accent(0.14f), 10.f);
        else ren.drawRoundedRect(r, {0.93f,0.94f,0.97f,0.88f * gMusicUiAlpha}, 10.f);
        const float sc = keys[(size_t)i].size() > 2 ? 0.48f : 0.72f;
        ren.drawText(keys[(size_t)i], {r.x + 13.f,r.y + 13.f}, m_smallFont,
                     i == m_keyboardIndex ? accent() : textPrimary(), sc);
    }
}

void MusicScreen::onRender(nxui::Renderer& ren) {
    if (!m_font || !m_smallFont) return;

    if (!m_active) {
        gMusicUiAlpha = 1.f;
        gMusicAccent = m_paletteAccent;
        drawHomeMiniModule(ren);
        return;
    }

    gMusicUiAlpha = std::clamp(m_transitionAlpha, 0.f, 1.f);
    gMusicAccent = m_paletteAccent;
    drawCrtBackground(ren);
    drawTopBar(ren);

    if (m_scanRunning) {
        drawEmpty(ren, "Analyse de la bibliothèque…",
                  std::to_string(m_tracksFound.load()) + " morceaux détectés  •  " +
                  std::to_string(m_filesVisited.load()) + " fichiers parcourus");
    } else {
        switch (m_view) {
            case View::Home: drawAlbums(ren); break;
            case View::Albums: drawAlbums(ren); break;
            case View::Artists: drawArtists(ren); break;
            case View::Tracks: drawTracks(ren); break;
            case View::Playlists: drawPlaylists(ren); break;
            case View::AlbumDetail: drawAlbumDetail(ren); break;
            case View::ArtistDetail: drawArtistDetail(ren); break;
            case View::PlaylistDetail: drawPlaylistDetail(ren); break;
            case View::NowPlaying: drawNowPlaying(ren); break;
            case View::Queue: drawQueue(ren); break;
            case View::SearchResults: drawTracks(ren, &m_searchResults, "Résultats  •  " + m_searchQuery); break;
        }
    }

    if (m_nextToastTimer > 0.f && currentTrack() && !m_client.queueTrackIds().empty()) {
        const int nextIndex = m_status.current_index + 1;
        if (nextIndex >= 0 && nextIndex < static_cast<int>(m_client.queueTrackIds().size())) {
            const Track* next = trackForId(m_client.queueTrackIds()[(size_t)nextIndex]);
            if (next) {
                nxui::Rect toast{812.f, 92.f, 406.f, 82.f};
                ren.drawRoundedRect(toast, {1.f,1.f,1.f,0.90f * gMusicUiAlpha}, 19.f);
                ren.drawRoundedRectOutline(toast, accent(0.22f), 19.f, 1.2f);
                ren.drawText("À suivre", {toast.x + 18.f,toast.y + 12.f}, m_smallFont, accent(), 0.73f);
                ren.drawText(next->title, {toast.x + 18.f,toast.y + 36.f}, m_smallFont, textPrimary(), 0.80f);
                ren.drawText(next->artist, {toast.x + 18.f,toast.y + 59.f}, m_smallFont, textSecondary(), 0.65f);
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
        return;
    }
    if (!input.touchUp() || !m_touchTracking) return;
    m_touchTracking = false;
    const float x = input.touchX(), y = input.touchY();
    const float dx = x - m_touchStartX;
    const float dy = y - m_touchStartY;

    if (m_modal != Modal::None) return;

    // Horizontal swipes directly drive the approved carousel.
    if ((m_view == View::Albums || m_view == View::Playlists) && std::abs(dx) > 55.f && std::abs(dx) > std::abs(dy)) {
        moveSelection(dx < 0.f ? 1 : -1, 0);
        return;
    }

    if (std::abs(dx) > 22.f || std::abs(dy) > 22.f) return;
    if (y >= 20.f && y <= 78.f && x >= 443.f && x <= 837.f) {
        setTab(x < 640.f ? 0 : 1);
        return;
    }

    if (m_view == View::Albums && y >= 155.f && y <= 520.f && x >= 460.f && x <= 820.f) {
        activateSelection();
        return;
    }
    if (m_view == View::Playlists && y >= 130.f && y <= 540.f && x >= 430.f && x <= 850.f) {
        activateSelection();
        return;
    }

    if (m_view == View::AlbumDetail || m_view == View::PlaylistDetail) {
        const size_t count = m_view == View::AlbumDetail && m_detailAlbum < m_library.albums.size()
            ? m_library.albums[m_detailAlbum].tracks.size()
            : (m_detailPlaylist < m_playlistStore.playlists().size()
                ? m_playlistStore.playlists()[m_detailPlaylist].trackIds.size() : 0);
        const int row = static_cast<int>((y - 218.f) / 51.f);
        if (x >= 558.f && x <= 1210.f && row >= 0 && row < 7) {
            const int start = visibleListStart(count, 7);
            const int idx = start + row;
            if (idx >= 0 && idx < static_cast<int>(count)) {
                m_selection = idx;
                activateSelection();
            }
        }
    }
}


} // namespace switchu::menu::music
