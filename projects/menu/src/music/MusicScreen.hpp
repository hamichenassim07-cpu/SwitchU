#pragma once

#include "MusicClient.hpp"
#include "MusicCoverCache.hpp"
#include "MusicLibrary.hpp"
#include "MusicPlaylistStore.hpp"

#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/widgets/Widget.hpp>

#include <atomic>
#include <functional>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>

namespace switchu::menu::music {

class MusicScreen : public nxui::Widget {
public:
    MusicScreen();
    ~MusicScreen() override;

    void setFonts(nxui::Font* normal, nxui::Font* small, nxui::Font* icons) {
        m_font = normal; m_smallFont = small; m_iconFont = icons;
    }
    void setProfileTexture(const nxui::Texture* texture) { m_profileTexture = texture; }
    void onClose(std::function<void()> cb) { m_closeCb = std::move(cb); }
    void onSessionGuard(std::function<void(bool)> cb) { m_sessionGuardCb = std::move(cb); }

    void show();
    void hide();
    bool isActive() const { return m_active; }
    void handleTouch(nxui::Input& input);

    bool hasMusicSession() const {
        return (m_status.flags & switchu::music::MusicStatus_SessionActive) != 0;
    }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    enum class View {
        Home,
        Albums,
        Artists,
        Tracks,
        Playlists,
        AlbumDetail,
        ArtistDetail,
        PlaylistDetail,
        NowPlaying,
        Queue,
        SearchResults,
    };
    enum class Modal {
        None,
        SearchKeyboard,
        PlaylistNameKeyboard,
        PlaylistChooser,
    };

    void setupActions();
    void startScan(bool force = false);
    void finishScanIfReady();
    void refreshStatus(bool force = false);
    void setTab(int index);
    void moveSelection(int dx, int dy);
    void activateSelection();
    void goBack();
    void contextualX();
    void contextualY();
    void openSearch();
    void openCreatePlaylist();
    void openPlaylistChooser(uint64_t trackId);
    void modalMove(int dx, int dy);
    void modalActivate();
    void modalBackspace();
    void modalConfirm();
    void modalCancel();

    void openAlbum(size_t index);
    void openArtist(size_t index);
    void openPlaylist(size_t index);
    void playTrackIds(const std::vector<uint64_t>& ids, int index);
    void playTrackIndices(const std::vector<size_t>& indices, int index);
    void updateNowPlayingSelection(int delta);
    void activateNowPlayingControl();
    void seekRelative(int64_t deltaMs);
    void appendToQueue(const std::vector<uint64_t>& ids);

    void refreshAdaptivePalette();
    void finishAdaptivePaletteIfReady();
    CoverRef selectedCover() const;
    CoverRef playlistCover(size_t playlistIndex) const;
    uint64_t playlistDurationMs(size_t playlistIndex) const;
    uint64_t albumDurationMs(size_t albumIndex) const;
    nxui::Color extractCoverAccent(const CoverRef& cover) const;
    void drawReflectedCover(nxui::Renderer& ren, const CoverRef& cover,
                            const nxui::Rect& rect, bool selected, int maxSide = 512);
    void drawHomeMiniModule(nxui::Renderer& ren);

    const Track* currentTrack() const;
    const Track* trackForId(uint64_t id) const;
    std::vector<uint64_t> albumTrackIds(size_t albumIndex) const;
    std::vector<uint64_t> artistTrackIds(size_t artistIndex) const;
    std::vector<uint64_t> playlistTrackIds(size_t playlistIndex) const;
    size_t selectedTrackIndex() const;

    void drawCrtBackground(nxui::Renderer& ren);
    void drawTopBar(nxui::Renderer& ren);
    void drawHome(nxui::Renderer& ren);
    void drawAlbums(nxui::Renderer& ren);
    void drawArtists(nxui::Renderer& ren);
    void drawTracks(nxui::Renderer& ren, const std::vector<size_t>* overrideTracks = nullptr,
                    const std::string& heading = {});
    void drawPlaylists(nxui::Renderer& ren);
    void drawAlbumDetail(nxui::Renderer& ren);
    void drawArtistDetail(nxui::Renderer& ren);
    void drawPlaylistDetail(nxui::Renderer& ren);
    void drawNowPlaying(nxui::Renderer& ren);
    void drawQueue(nxui::Renderer& ren);
    void drawMiniPlayer(nxui::Renderer& ren);
    void drawBottomHints(nxui::Renderer& ren);
    void drawModal(nxui::Renderer& ren);
    void drawCover(nxui::Renderer& ren, const CoverRef& cover,
                   const nxui::Rect& rect, bool selected, int maxSide = 256);
    void drawTrackRow(nxui::Renderer& ren, const Track& track, const nxui::Rect& row,
                      bool selected, int ordinal = 0);
    void drawEmpty(nxui::Renderer& ren, const std::string& title,
                   const std::string& detail);

    std::string formatDuration(uint64_t ms) const;
    std::string formatClock() const;
    void updateBattery(float dt);
    int visibleListStart(size_t count, int rows) const;
    void clampSelectionForView();

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    nxui::Font* m_iconFont = nullptr;
    const nxui::Texture* m_profileTexture = nullptr;
    std::function<void()> m_closeCb;
    std::function<void(bool)> m_sessionGuardCb;

    bool m_active = false;
    View m_view = View::Home;
    View m_returnView = View::Home;
    int m_tabIndex = 0;
    int m_selection = 0;
    int m_homeSection = 0;
    size_t m_detailAlbum = 0;
    size_t m_detailArtist = 0;
    size_t m_detailPlaylist = 0;
    int m_nowControl = 2;

    LibrarySnapshot m_library;
    MusicPlaylistStore m_playlistStore;
    MusicClient m_client;
    MusicCoverCache m_coverCache{10};

    std::future<LibrarySnapshot> m_scanFuture;
    std::atomic<uint32_t> m_filesVisited{0};
    std::atomic<uint32_t> m_tracksFound{0};
    bool m_scanRunning = false;
    bool m_hasScanned = false;

    switchu::music::Status m_status{};
    float m_statusTimer = 0.f;
    float m_hiddenGuardTimer = 0.f;
    float m_batteryTimer = 0.f;
    uint32_t m_batteryPercent = 100;
    bool m_batteryCharging = false;
    float m_uiTime = 0.f;
    bool m_nextSoonWasVisible = false;
    float m_nextToastTimer = 0.f;

    // V0.02 visual transition and deferred GPU resource release.
    float m_transitionAlpha = 0.f;
    bool m_closing = false;
    float m_hiddenCoverReleaseTimer = 0.f;
    bool m_lastSessionGuardState = false;

    // Cover-adaptive light music palette.
    nxui::Color m_paletteAccent {0.46f, 0.31f, 0.92f, 1.f};
    nxui::Color m_paletteTargetAccent {0.46f, 0.31f, 0.92f, 1.f};
    nxui::Color m_paletteSoft {0.90f, 0.92f, 0.98f, 1.f};
    std::string m_paletteCoverKey;
    std::string m_paletteRequestedKey;
    CoverRef m_paletteRequestedCover;
    struct PaletteJobResult { std::string key; nxui::Color color; };
    std::future<PaletteJobResult> m_paletteFuture;
    bool m_paletteRunning = false;
    std::unordered_map<std::string, nxui::Color> m_paletteCache;

    std::vector<size_t> m_searchResults;
    std::string m_searchQuery;

    Modal m_modal = Modal::None;
    std::string m_keyboardText;
    int m_keyboardIndex = 0;
    uint64_t m_pendingPlaylistTrackId = 0;
    int m_modalSelection = 0;

    bool m_touchTracking = false;
    float m_touchStartX = 0.f;
    float m_touchStartY = 0.f;
};

} // namespace switchu::menu::music
