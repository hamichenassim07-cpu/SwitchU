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

namespace switchu::menu::music {
namespace {

constexpr float kScreenW = 1280.f;
constexpr float kScreenH = 720.f;
constexpr float kTopY = 22.f;
constexpr float kContentTop = 108.f;
constexpr float kMiniY = 626.f;
constexpr float kMiniH = 58.f;
constexpr float kBottomY = 691.f;
constexpr int kAlbumCols = 5;
constexpr int kAlbumRows = 2;
constexpr int kListRows = 8;

const char* kTabs[] = {"Accueil", "Albums", "Artistes", "Morceaux", "Playlists"};
constexpr int kTabCount = 5;

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

nxui::Color textPrimary(float a = 1.f) { return {0.96f, 0.97f, 0.99f, a}; }
nxui::Color textSecondary(float a = 1.f) { return {0.62f, 0.67f, 0.74f, a}; }
nxui::Color accent(float a = 1.f) { return {0.13f, 0.78f, 0.98f, a}; }
nxui::Color panel(float a = 1.f) { return {0.055f, 0.064f, 0.075f, a}; }
nxui::Color panel2(float a = 1.f) { return {0.075f, 0.087f, 0.101f, a}; }

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
    // Keep the widget in the tree even while visually hidden: its tiny
    // background-session guard prevents the HOME theme from starting on top
    // of daemon-owned local music after returning from a game.
    setVisible(true);
    setOpacity(0.f);
    setFocusable(false);
    setFrameworkTouchEnabled(false);
    setTag("switchu_music_v001");
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
    m_active = true;
    setVisible(true);
    setOpacity(1.f);
    setFocusable(true);
    m_view = View::Home;
    m_tabIndex = 0;
    m_selection = 0;
    m_homeSection = 0;
    m_modal = Modal::None;
    m_client.loadQueueTrackIds();
    refreshStatus(true);
    if (!m_hasScanned) startScan(false);
}

void MusicScreen::hide() {
    m_active = false;
    setVisible(true);
    setOpacity(0.f);
    setFocusable(false);
    m_modal = Modal::None;
    m_coverCache.clear();
}

void MusicScreen::startScan(bool force) {
    if (m_scanRunning || (m_hasScanned && !force)) return;
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

    m_library = m_scanFuture.get();
    m_scanRunning = false;
    m_hasScanned = true;
    m_playlistStore.load(m_library);
    m_playlistStore.pruneMissing(m_library);
    m_playlistStore.save();
    m_client.loadQueueTrackIds();
    clampSelectionForView();
}

void MusicScreen::refreshStatus(bool force) {
    if (!force && m_statusTimer < 0.25f) return;
    m_statusTimer = 0.f;
    switchu::music::Status fresh{};
    if (m_client.getStatus(fresh)) {
        const bool nextSoon = statusFlag(fresh, switchu::music::MusicStatus_NextSoon);
        if (nextSoon && !m_nextSoonWasVisible)
            m_nextToastTimer = 3.5f;
        m_nextSoonWasVisible = nextSoon;
        m_status = fresh;
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
    if (!m_active) {
        m_hiddenGuardTimer += dt;
        if (m_hiddenGuardTimer >= 0.50f) {
            m_hiddenGuardTimer = 0.f;
            std::error_code ec;
            if (std::filesystem::exists(switchu::music::kSessionFlagPath, ec) &&
                m_sessionGuardCb)
                m_sessionGuardCb();
        }
        return;
    }
    m_hiddenGuardTimer = 0.f;
    m_uiTime += dt;
    m_statusTimer += dt;
    if (m_nextToastTimer > 0.f) m_nextToastTimer = std::max(0.f, m_nextToastTimer - dt);
    finishScanIfReady();
    refreshStatus(false);
    updateBattery(dt);
}

void MusicScreen::setTab(int index) {
    index = (index % kTabCount + kTabCount) % kTabCount;
    m_tabIndex = index;
    m_selection = 0;
    m_homeSection = 0;
    switch (index) {
        case 0: m_view = View::Home; break;
        case 1: m_view = View::Albums; break;
        case 2: m_view = View::Artists; break;
        case 3: m_view = View::Tracks; break;
        case 4: m_view = View::Playlists; break;
    }
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
    if (m_view == View::Home) {
        if (dy != 0) {
            m_homeSection = std::clamp(m_homeSection + dy, 0, 3);
            m_selection = 0;
        } else if (dx != 0) {
            int max = 0;
            if (m_homeSection == 0) max = currentTrack() ? 1 : 0;
            else if (m_homeSection == 1) max = static_cast<int>(std::min<size_t>(5, m_library.recentAlbums.size()));
            else if (m_homeSection == 2) max = static_cast<int>(std::min<size_t>(5, m_library.albums.size()));
            else max = static_cast<int>(std::min<size_t>(3, m_playlistStore.playlists().size()));
            if (max > 0) m_selection = std::clamp(m_selection + dx, 0, max - 1);
        }
        return;
    }
    if (m_view == View::Albums) {
        m_selection += dx + dy * kAlbumCols;
    } else if (m_view == View::NowPlaying) {
        if (dx != 0) updateNowPlayingSelection(dx);
        else if (dy != 0) {
            float v = std::clamp(m_status.volume - dy * 0.05f, 0.f, 1.f);
            m_client.setVolume(v);
            m_status.volume = v;
        }
        return;
    } else {
        m_selection += dy != 0 ? dy : dx;
    }
    clampSelectionForView();
}

void MusicScreen::openAlbum(size_t index) {
    if (index >= m_library.albums.size()) return;
    m_returnView = m_view;
    m_detailAlbum = index;
    m_view = View::AlbumDetail;
    m_selection = 0;
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
    m_returnView = m_view;
    m_detailPlaylist = index;
    m_view = View::PlaylistDetail;
    m_selection = 0;
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
    const float volume = std::clamp(m_status.volume, 0.f, 1.f);
    const bool shuffle = statusFlag(m_status, switchu::music::MusicStatus_Shuffle);
    auto repeat = static_cast<switchu::music::RepeatMode>(m_status.repeat_mode);
    if (m_client.writeQueueIds(m_library, ids, index, volume, shuffle, repeat) &&
        m_client.reloadQueue()) {
        m_client.loadQueueTrackIds();
        m_client.playIndex(std::clamp(index, 0, static_cast<int>(ids.size()) - 1));
        refreshStatus(true);
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
            if (m_homeSection == 0 && currentTrack()) {
                m_returnView = View::Home; m_view = View::NowPlaying; m_selection = 0;
            } else if (m_homeSection == 1 && m_selection < static_cast<int>(m_library.recentAlbums.size())) {
                openAlbum(m_library.recentAlbums[static_cast<size_t>(m_selection)]);
            } else if (m_homeSection == 2 && m_selection < static_cast<int>(m_library.albums.size())) {
                openAlbum(static_cast<size_t>(m_selection));
            } else if (m_homeSection == 3 && m_selection < static_cast<int>(m_playlistStore.playlists().size())) {
                openPlaylist(static_cast<size_t>(m_selection));
            }
            break;
        case View::Albums: openAlbum(static_cast<size_t>(m_selection)); break;
        case View::Artists: openArtist(static_cast<size_t>(m_selection)); break;
        case View::Tracks: {
            if (m_selection >= 0 && static_cast<size_t>(m_selection) < m_library.tracks.size()) {
                std::vector<uint64_t> ids; ids.reserve(m_library.tracks.size());
                for (const auto& t : m_library.tracks) ids.push_back(t.id);
                playTrackIds(ids, m_selection);
            }
            break;
        }
        case View::Playlists: openPlaylist(static_cast<size_t>(m_selection)); break;
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
            if (!m_client.queueTrackIds().empty()) m_client.playIndex(m_selection);
            break;
        case View::NowPlaying: activateNowPlayingControl(); break;
    }
}

void MusicScreen::goBack() {
    switch (m_view) {
        case View::Home:
        case View::Albums:
        case View::Artists:
        case View::Tracks:
        case View::Playlists:
            if (m_closeCb) m_closeCb(); else hide();
            return;
        case View::NowPlaying:
        case View::Queue:
        case View::SearchResults:
        case View::AlbumDetail:
        case View::ArtistDetail:
        case View::PlaylistDetail:
            m_view = m_returnView;
            m_selection = 0;
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
    if (m_view == View::PlaylistDetail) {
        if (m_detailPlaylist < m_playlistStore.playlists().size() &&
            m_selection >= 0 && static_cast<size_t>(m_selection) < m_playlistStore.playlists()[m_detailPlaylist].trackIds.size()) {
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
    if (!m_client.queueTrackIds().empty()) {
        m_returnView = m_view;
        m_view = View::Queue;
        m_selection = std::max(0, m_status.current_index);
        clampSelectionForView();
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
        m_tabIndex = 4;
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

void MusicScreen::drawCrtBackground(nxui::Renderer& ren) {
    ren.drawRect({0, 0, kScreenW, kScreenH}, {0.042f, 0.047f, 0.055f, 1.f});
    constexpr int bands = 8;
    constexpr int slices = 12;
    const float bw = kScreenW / bands;
    for (int b = 0; b < bands; ++b) {
        for (int s = 0; s < slices; ++s) {
            const float sy = kScreenH * s / slices;
            const float sh = kScreenH / slices + 1.f;
            const float norm = (sy + sh * 0.5f - kScreenH * 0.5f) / (kScreenH * 0.5f);
            const float curve = norm * norm * 9.f;
            const float x = b * bw + ((b & 1) ? curve : -curve);
            const float lum = (b & 1) ? 0.068f : 0.052f;
            ren.drawRect({x, sy, bw + 12.f, sh}, {lum, lum + 0.004f, lum + 0.010f, 1.f});
        }
    }
    for (int y = 0; y < 720; y += 8)
        ren.drawRect({0.f, static_cast<float>(y), kScreenW, 1.f}, {0.f, 0.f, 0.f, 0.13f});
    ren.drawRect({0, 0, kScreenW, 76.f}, {0.015f, 0.020f, 0.026f, 0.34f});
}

void MusicScreen::drawTopBar(nxui::Renderer& ren) {
    if (!m_font || !m_smallFont) return;
    ren.drawText("Musique", {38.f, 29.f}, m_font, textPrimary(), 1.05f);

    const float tabX = 300.f;
    const float tabW = 127.f;
    for (int i = 0; i < kTabCount; ++i) {
        const bool active = (m_view <= View::Playlists) && i == m_tabIndex;
        nxui::Rect r{tabX + i * tabW, 20.f, tabW - 8.f, 46.f};
        if (active) ren.drawRoundedRect(r, {0.08f, 0.22f, 0.28f, 0.76f}, 16.f);
        ren.drawText(kTabs[i], {r.x + 14.f, r.y + 13.f}, m_smallFont,
                     active ? accent() : textSecondary(), 0.84f);
    }

    // Profile glyph: intentionally simple and local to the app, matching the
    // HOME's clean monochrome HUD without duplicating the HOME avatar texture.
    const float px = 1088.f, py = 43.f;
    ren.drawCircle({px, py}, 22.f, {0.07f, 0.09f, 0.11f, 0.88f}, 48);
    ren.drawCircle({px, py - 5.f}, 6.2f, textPrimary(0.94f), 36);
    ren.drawRoundedRect({px - 10.f, py + 4.f, 20.f, 9.f}, textPrimary(0.94f), 5.f);

    const std::string clock = formatClock();
    ren.drawText(clock, {1131.f, 30.f}, m_font, textPrimary(), 0.86f);

    const float level = std::clamp(m_batteryPercent / 100.f, 0.f, 1.f);
    nxui::Rect body{1200.f, 32.f, 42.f, 20.f};
    ren.drawRoundedRectOutline(body, textPrimary(0.9f), 4.f, 1.5f);
    ren.drawRoundedRect({1244.f, 38.f, 4.f, 8.f}, textPrimary(0.8f), 1.5f);
    nxui::Rect fill{1203.f, 35.f, 36.f * level, 14.f};
    nxui::Color bc = level <= 0.20f ? nxui::Color{1.f,0.24f,0.22f,0.95f}
                    : level <= 0.50f ? nxui::Color{1.f,0.78f,0.18f,0.95f}
                    : nxui::Color{0.28f,0.92f,0.42f,0.95f};
    if (m_batteryCharging) bc = accent(0.95f);
    if (fill.width > 0.5f) ren.drawRoundedRect(fill, bc, 2.5f);
}

void MusicScreen::drawCover(nxui::Renderer& ren, const CoverRef& cover,
                            const nxui::Rect& r, bool selected, int maxSide) {
    const nxui::Texture* tex = m_coverCache.get(cover, ren, maxSide);
    if (tex) {
        ren.drawTextureRounded(tex, r, 12.f, {1.f,1.f,1.f,1.f});
    } else {
        ren.drawRoundedRect(r, {0.10f, 0.12f, 0.15f, 1.f}, 12.f);
        if (m_font) {
            const auto m = m_font->measure("♪");
            ren.drawText("♪", {r.x + (r.width - m.x * 1.6f) * 0.5f,
                               r.y + (r.height - m.y * 1.6f) * 0.5f - 4.f},
                         m_font, textSecondary(0.78f), 1.6f);
        }
    }
    if (selected)
        ren.drawRoundedRectOutline({r.x - 3.f, r.y - 3.f, r.width + 6.f, r.height + 6.f},
                                   accent(0.95f), 14.f, 2.2f);
}

void MusicScreen::drawEmpty(nxui::Renderer& ren, const std::string& title,
                            const std::string& detail) {
    if (!m_font || !m_smallFont) return;
    ren.drawText(title, {72.f, 255.f}, m_font, textPrimary(), 1.25f);
    ren.drawText(detail, {72.f, 302.f}, m_smallFont, textSecondary(), 0.90f);
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
    const int start = std::max(0, (m_selection / kAlbumCols - 1) * kAlbumCols);
    for (int slot = 0; slot < kAlbumCols * kAlbumRows; ++slot) {
        const int idx = start + slot;
        if (idx >= static_cast<int>(m_library.albums.size())) break;
        const int col = slot % kAlbumCols, row = slot / kAlbumCols;
        const float x = 62.f + col * 236.f;
        const float y = 120.f + row * 236.f;
        const auto& a = m_library.albums[(size_t)idx];
        drawCover(ren, a.cover, {x, y, 165.f, 165.f}, idx == m_selection, 256);
        ren.drawText(a.title, {x, y + 174.f}, m_smallFont, textPrimary(), 0.80f);
        ren.drawText(a.artist, {x, y + 198.f}, m_smallFont, textSecondary(), 0.70f);
    }
}

int MusicScreen::visibleListStart(size_t count, int rows) const {
    if (count <= static_cast<size_t>(rows)) return 0;
    int start = m_selection - rows / 2;
    return std::clamp(start, 0, static_cast<int>(count) - rows);
}

void MusicScreen::drawTrackRow(nxui::Renderer& ren, const Track& t, const nxui::Rect& row,
                               bool selected, int ordinal) {
    if (selected) ren.drawRoundedRect(row, panel2(0.96f), 12.f);
    if (ordinal > 0) {
        char num[12]{}; std::snprintf(num, sizeof(num), "%d", ordinal);
        ren.drawText(num, {row.x + 12.f, row.y + 14.f}, m_smallFont, textSecondary(), 0.72f);
    }
    ren.drawText(t.title, {row.x + 52.f, row.y + 10.f}, m_smallFont, textPrimary(), 0.78f);
    ren.drawText(t.artist + " · " + t.album, {row.x + 52.f, row.y + 31.f},
                 m_smallFont, textSecondary(), 0.65f);
    ren.drawText(formatDuration(t.durationMs), {row.x + row.width - 72.f, row.y + 18.f},
                 m_smallFont, textSecondary(), 0.66f);
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
    if (m_playlistStore.playlists().empty()) {
        drawEmpty(ren, "Aucune playlist", "Appuie sur + pour créer ta première playlist locale.");
        return;
    }
    const auto& ps = m_playlistStore.playlists();
    const int start = visibleListStart(ps.size(), kListRows);
    for (int i = 0; i < kListRows; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(ps.size())) break;
        nxui::Rect row{70.f, 130.f + i * 58.f, 1140.f, 52.f};
        if (idx == m_selection) ren.drawRoundedRect(row, panel2(), 12.f);
        ren.drawRoundedRect({row.x + 9.f,row.y + 7.f,38.f,38.f}, {0.10f,0.20f,0.25f,1.f}, 9.f);
        ren.drawText("♪", {row.x + 20.f,row.y + 12.f}, m_font, accent(), 0.78f);
        ren.drawText(ps[(size_t)idx].name, {row.x + 61.f,row.y + 12.f}, m_font, textPrimary(), 0.82f);
        ren.drawText(std::to_string(ps[(size_t)idx].trackIds.size()) + " morceaux",
                     {row.x + 890.f,row.y + 16.f}, m_smallFont, textSecondary(), 0.70f);
    }
}

void MusicScreen::drawAlbumDetail(nxui::Renderer& ren) {
    if (m_detailAlbum >= m_library.albums.size()) return;
    const auto& a = m_library.albums[m_detailAlbum];
    drawCover(ren, a.cover, {65.f, 128.f, 230.f, 230.f}, false, 320);
    ren.drawText(a.title, {335.f, 144.f}, m_font, textPrimary(), 1.30f);
    ren.drawText(a.artist, {335.f, 190.f}, m_font, accent(), 0.95f);
    std::string info = std::to_string(a.tracks.size()) + " morceaux";
    if (a.year > 0) info += " · " + std::to_string(a.year);
    ren.drawText(info, {335.f, 231.f}, m_smallFont, textSecondary(), 0.78f);
    const int start = visibleListStart(a.tracks.size(), 5);
    for (int i = 0; i < 5; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(a.tracks.size())) break;
        const size_t ti = a.tracks[(size_t)idx];
        if (ti < m_library.tracks.size())
            drawTrackRow(ren, m_library.tracks[ti], {335.f, 275.f + i * 58.f, 870.f, 52.f}, idx == m_selection,
                         m_library.tracks[ti].trackNumber > 0 ? m_library.tracks[ti].trackNumber : idx + 1);
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
    ren.drawText(p.name, {65.f, 112.f}, m_font, textPrimary(), 1.25f);
    ren.drawText(std::to_string(p.trackIds.size()) + " morceaux · X retire le morceau sélectionné",
                 {65.f, 156.f}, m_smallFont, textSecondary(), 0.74f);
    const int start = visibleListStart(p.trackIds.size(), 7);
    for (int i = 0; i < 7; ++i) {
        const int idx = start + i;
        if (idx >= static_cast<int>(p.trackIds.size())) break;
        const Track* t = trackForId(p.trackIds[(size_t)idx]);
        if (t) drawTrackRow(ren, *t, {70.f, 193.f + i * 56.f, 1140.f, 51.f}, idx == m_selection, idx + 1);
    }
}

void MusicScreen::drawNowPlaying(nxui::Renderer& ren) {
    const Track* t = currentTrack();
    if (!t) {
        drawEmpty(ren, "Aucune lecture en cours", "Choisis un morceau dans ta bibliothèque.");
        return;
    }
    drawCover(ren, t->cover, {90.f, 145.f, 345.f, 345.f}, false, 420);
    ren.drawText(t->title, {500.f, 163.f}, m_font, textPrimary(), 1.48f);
    ren.drawText(t->artist, {500.f, 215.f}, m_font, accent(), 1.00f);
    ren.drawText(t->album, {500.f, 253.f}, m_smallFont, textSecondary(), 0.82f);

    const float progress = m_status.duration_ms > 0
        ? std::clamp(float(double(m_status.position_ms) / double(m_status.duration_ms)), 0.f, 1.f) : 0.f;
    ren.drawRoundedRect({500.f, 323.f, 650.f, 7.f}, {0.16f,0.18f,0.21f,1.f}, 3.5f);
    ren.drawRoundedRect({500.f, 323.f, 650.f * progress, 7.f}, accent(), 3.5f);
    ren.drawText(formatDuration(m_status.position_ms), {500.f, 340.f}, m_smallFont, textSecondary(), 0.70f);
    ren.drawText(formatDuration(m_status.duration_ms), {1098.f, 340.f}, m_smallFont, textSecondary(), 0.70f);

    const std::array<std::string,5> labels = {
        statusFlag(m_status, switchu::music::MusicStatus_Shuffle) ? "⇄" : "↝",
        "◀◀",
        statusFlag(m_status, switchu::music::MusicStatus_Playing) ? "Ⅱ" : "▶",
        "▶▶",
        repeatLabel(m_status.repeat_mode)
    };
    for (int i = 0; i < 5; ++i) {
        const float x = 545.f + i * 130.f;
        nxui::Rect b{x, 397.f, 78.f, 58.f};
        if (i == m_nowControl) ren.drawRoundedRect(b, {0.09f,0.24f,0.30f,0.96f}, 19.f);
        ren.drawText(labels[(size_t)i], {b.x + 21.f, b.y + 13.f}, m_font,
                     i == m_nowControl ? accent() : textPrimary(), 0.92f);
    }
    ren.drawText("Volume " + std::to_string(int(std::round(m_status.volume * 100.f))) + "%",
                 {500.f, 490.f}, m_smallFont, textSecondary(), 0.76f);
    ren.drawRoundedRect({610.f, 498.f, 430.f, 5.f}, {0.16f,0.18f,0.21f,1.f}, 2.5f);
    ren.drawRoundedRect({610.f, 498.f, 430.f * std::clamp(m_status.volume,0.f,1.f), 5.f}, accent(), 2.5f);
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
    const Track* t = currentTrack();
    if (!t || m_view == View::NowPlaying) return;
    nxui::Rect bar{30.f, kMiniY, 1220.f, kMiniH};
    ren.drawRoundedRect(bar, {0.038f,0.046f,0.057f,0.97f}, 17.f);
    drawCover(ren, t->cover, {bar.x + 8.f,bar.y + 7.f,44.f,44.f}, false, 96);
    ren.drawText(t->title, {bar.x + 66.f,bar.y + 10.f}, m_smallFont, textPrimary(), 0.76f);
    ren.drawText(t->artist, {bar.x + 66.f,bar.y + 31.f}, m_smallFont, textSecondary(), 0.64f);
    ren.drawText(statusFlag(m_status, switchu::music::MusicStatus_Playing) ? "Ⅱ" : "▶",
                 {bar.x + 1035.f,bar.y + 13.f}, m_font, textPrimary(), 0.88f);
    ren.drawText("▶▶", {bar.x + 1115.f,bar.y + 14.f}, m_font, textPrimary(), 0.78f);
    const float prog = m_status.duration_ms > 0
        ? std::clamp(float(double(m_status.position_ms) / double(m_status.duration_ms)),0.f,1.f) : 0.f;
    ren.drawRect({bar.x + 2.f, bar.y, (bar.width - 4.f) * prog, 2.f}, accent());
}

void MusicScreen::drawBottomHints(nxui::Renderer& ren) {
    if (!m_smallFont) return;
    std::vector<std::pair<nxui::Button,std::string>> hints;
    if (m_modal == Modal::SearchKeyboard || m_modal == Modal::PlaylistNameKeyboard) {
        hints = {{nxui::Button::A,"Saisir"},{nxui::Button::X,"Effacer"},{nxui::Button::Plus,"Valider"},{nxui::Button::B,"Annuler"}};
    } else if (m_modal == Modal::PlaylistChooser) {
        hints = {{nxui::Button::A,"Ajouter"},{nxui::Button::B,"Annuler"}};
    } else if (m_view == View::NowPlaying) {
        hints = {{nxui::Button::A,"Action"},{nxui::Button::ZL,"-10 s"},{nxui::Button::ZR,"+10 s"},{nxui::Button::Y,"File"},{nxui::Button::B,"Retour"}};
    } else {
        hints = {{nxui::Button::A,"Sélectionner"},{nxui::Button::X,"Recherche / playlist"},{nxui::Button::Y,"File"},{nxui::Button::Plus,"Nouvelle playlist"},{nxui::Button::B,"Retour"}};
    }
    float x = 45.f;
    for (const auto& h : hints) {
        if (m_iconFont) {
            const std::string g = buttonGlyph(h.first);
            ren.drawText(g, {x, kBottomY}, m_iconFont, textPrimary(), 0.74f);
            x += 30.f;
        }
        ren.drawText(h.second, {x, kBottomY + 2.f}, m_smallFont, textSecondary(), 0.66f);
        x += std::max(105.f, m_smallFont->measure(h.second).x * 0.66f + 32.f);
    }
}

void MusicScreen::drawModal(nxui::Renderer& ren) {
    if (m_modal == Modal::None || !m_font || !m_smallFont) return;
    ren.drawRect({0,0,kScreenW,kScreenH}, {0.f,0.f,0.f,0.55f});
    nxui::Rect p{180.f, 115.f, 920.f, 480.f};
    ren.drawRoundedRect(p, {0.045f,0.054f,0.066f,0.99f}, 26.f);
    ren.drawRoundedRectOutline(p, {0.22f,0.28f,0.34f,0.8f}, 26.f, 1.4f);

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
    ren.drawRoundedRect({220.f, 191.f, 840.f, 52.f}, {0.075f,0.088f,0.103f,1.f}, 13.f);
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
        if (i == m_keyboardIndex) ren.drawRoundedRect(r, {0.09f,0.24f,0.30f,1.f}, 10.f);
        else ren.drawRoundedRect(r, {0.065f,0.075f,0.089f,0.85f}, 10.f);
        const float sc = keys[(size_t)i].size() > 2 ? 0.48f : 0.72f;
        ren.drawText(keys[(size_t)i], {r.x + 13.f,r.y + 13.f}, m_smallFont,
                     i == m_keyboardIndex ? accent() : textPrimary(), sc);
    }
}

void MusicScreen::onRender(nxui::Renderer& ren) {
    if (!m_active) return;
    drawCrtBackground(ren);
    drawTopBar(ren);

    if (m_scanRunning) {
        drawEmpty(ren, "Analyse de la bibliothèque…",
                  std::to_string(m_tracksFound.load()) + " morceaux détectés · " +
                  std::to_string(m_filesVisited.load()) + " fichiers parcourus");
    } else {
        switch (m_view) {
            case View::Home: drawHome(ren); break;
            case View::Albums: drawAlbums(ren); break;
            case View::Artists: drawArtists(ren); break;
            case View::Tracks: drawTracks(ren); break;
            case View::Playlists: drawPlaylists(ren); break;
            case View::AlbumDetail: drawAlbumDetail(ren); break;
            case View::ArtistDetail: drawArtistDetail(ren); break;
            case View::PlaylistDetail: drawPlaylistDetail(ren); break;
            case View::NowPlaying: drawNowPlaying(ren); break;
            case View::Queue: drawQueue(ren); break;
            case View::SearchResults: drawTracks(ren, &m_searchResults, "Résultats · " + m_searchQuery); break;
        }
    }

    drawMiniPlayer(ren);

    if (m_nextToastTimer > 0.f && currentTrack() && !m_client.queueTrackIds().empty()) {
        const int nextIndex = m_status.current_index + 1;
        if (nextIndex >= 0 && nextIndex < static_cast<int>(m_client.queueTrackIds().size())) {
            const Track* next = trackForId(m_client.queueTrackIds()[(size_t)nextIndex]);
            if (next) {
                nxui::Rect toast{820.f, 82.f, 415.f, 82.f};
                ren.drawRoundedRect(toast, {0.035f,0.045f,0.055f,0.97f}, 18.f);
                ren.drawText("À suivre", {toast.x + 18.f,toast.y + 13.f}, m_smallFont, accent(), 0.69f);
                ren.drawText(next->title, {toast.x + 18.f,toast.y + 35.f}, m_smallFont, textPrimary(), 0.75f);
                ren.drawText(next->artist, {toast.x + 18.f,toast.y + 57.f}, m_smallFont, textSecondary(), 0.62f);
            }
        }
    }

    drawBottomHints(ren);
    drawModal(ren);
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
    if (std::abs(x - m_touchStartX) > 22.f || std::abs(y - m_touchStartY) > 22.f) return;

    if (m_modal != Modal::None) return; // modal remains controller-first in V0.01.

    if (y >= 20.f && y <= 70.f && x >= 300.f && x < 300.f + kTabCount * 127.f) {
        setTab(static_cast<int>((x - 300.f) / 127.f));
        return;
    }
    if (y >= kMiniY && y <= kMiniY + kMiniH && currentTrack()) {
        m_returnView = m_view; m_view = View::NowPlaying; m_selection = 0; return;
    }

    if (m_view == View::Albums) {
        const int col = static_cast<int>((x - 62.f) / 236.f);
        const int row = static_cast<int>((y - 120.f) / 236.f);
        if (col >= 0 && col < kAlbumCols && row >= 0 && row < kAlbumRows) {
            const int start = std::max(0, (m_selection / kAlbumCols - 1) * kAlbumCols);
            const int idx = start + row * kAlbumCols + col;
            if (idx >= 0 && idx < static_cast<int>(m_library.albums.size())) {
                m_selection = idx; activateSelection();
            }
        }
    } else if (m_view == View::Tracks || m_view == View::Artists || m_view == View::Playlists ||
               m_view == View::Queue || m_view == View::SearchResults) {
        const float baseY = m_view == View::Queue ? 142.f : (m_view == View::Tracks || m_view == View::SearchResults ? 137.f : 130.f);
        const float step = (m_view == View::Queue) ? 54.f : (m_view == View::Tracks || m_view == View::SearchResults ? 56.f : 58.f);
        const int row = static_cast<int>((y - baseY) / step);
        if (row >= 0 && row < 8) {
            size_t count = m_view == View::Tracks ? m_library.tracks.size() :
                           m_view == View::Artists ? m_library.artists.size() :
                           m_view == View::Playlists ? m_playlistStore.playlists().size() :
                           m_view == View::Queue ? m_client.queueTrackIds().size() : m_searchResults.size();
            const int start = visibleListStart(count, 8);
            const int idx = start + row;
            if (idx >= 0 && idx < static_cast<int>(count)) { m_selection = idx; activateSelection(); }
        }
    }
}

} // namespace switchu::menu::music
