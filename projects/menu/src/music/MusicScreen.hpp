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
    // V0.02 intentionally keeps only the approved primary Music surfaces.
    // Queue is a supporting sub-view of Now Playing, not a fifth root category.
    enum class View {
        Albums,
        Playlists,
        AlbumDetail,
        PlaylistDetail,
        NowPlaying,
        Queue,
    };
    enum class Modal {
        None,
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
    void openCreatePlaylist();
    void openPlaylistChooser(uint64_t trackId);
    void modalMove(int dx, int dy);
    void modalActivate();
    void modalBackspace();
    void modalConfirm();
    void modalCancel();

    void openAlbum(size_t index);
    void openPlaylist(size_t index);
    void playTrackIds(const std::vector<uint64_t>& ids, int index);
    void updateNowPlayingSelection(int delta);
    void activateNowPlayingControl();
    void seekRelative(int64_t deltaMs);
    void appendToQueue(const std::vector<uint64_t>& ids);
    void openNowPlaying();

    CoverRef playlistCover(size_t playlistIndex) const;
    uint64_t playlistDurationMs(size_t playlistIndex) const;
    uint64_t albumDurationMs(size_t albumIndex) const;
    void drawReflectedCover(nxui::Renderer& ren, const CoverRef& cover,
                            const nxui::Rect& rect, bool selected, int maxSide = 512);

    const Track* currentTrack() const;
    const Track* trackForId(uint64_t id) const;
    std::vector<uint64_t> albumTrackIds(size_t albumIndex) const;
    std::vector<uint64_t> playlistTrackIds(size_t playlistIndex) const;
    size_t selectedTrackIndex() const;

    void drawCrtBackground(nxui::Renderer& ren);
    void drawTopBar(nxui::Renderer& ren);
    void drawAlbums(nxui::Renderer& ren);
    void drawPlaylists(nxui::Renderer& ren);
    void drawAlbumDetail(nxui::Renderer& ren);
    void drawPlaylistDetail(nxui::Renderer& ren);
    void drawNowPlaying(nxui::Renderer& ren);
    void drawQueue(nxui::Renderer& ren);
    void drawBottomHints(nxui::Renderer& ren);
    void drawModal(nxui::Renderer& ren);
    void drawCover(nxui::Renderer& ren, const CoverRef& cover,
                   const nxui::Rect& rect, bool selected, int maxSide = 256);
    void drawTrackRow(nxui::Renderer& ren, const Track& track, const nxui::Rect& row,
                      bool selected, int ordinal = 0);
    void drawNowPlayingIndicator(nxui::Renderer& ren, const nxui::Rect& row);
    void drawSecondaryMusicTabs(nxui::Renderer& ren);
    void drawEmpty(nxui::Renderer& ren, const std::string& title,
                   const std::string& detail);

    std::string formatDuration(uint64_t ms) const;
    std::string formatClock() const;
    std::string fitText(nxui::Font* font, const std::string& text,
                        float maxWidth, float scale) const;
    void updateBattery(float dt);
    int visibleListStart(size_t count, int rows) const;
    float visibleListStartVisual(size_t count, int rows) const;
    void clampSelectionForView();
    bool contentTransitionBusy() const;
    bool rootView() const;

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    nxui::Font* m_iconFont = nullptr;
    const nxui::Texture* m_profileTexture = nullptr;
    std::function<void()> m_closeCb;
    std::function<void(bool)> m_sessionGuardCb;

    bool m_active = false;
    View m_view = View::Albums;
    View m_nowPlayingReturnView = View::Albums;
    View m_queueReturnView = View::NowPlaying;
    int m_tabIndex = 0;
    int m_selection = 0;
    size_t m_detailAlbum = 0;
    size_t m_detailPlaylist = 0;
    int m_nowControl = 2;

    // HOME-derived motion state. Root carousel selection uses a continuous
    // visual position so covers glide instead of snapping between slots.
    float m_carouselVisualIndex = 0.f;
    float m_listVisualSelection = 0.f;
    float m_detailTransition = 1.f;
    bool m_detailClosing = false;
    View m_detailReturnView = View::Albums;
    float m_nowPlayingEnter = 1.f;
    bool m_nowPlayingClosing = false;
    int m_nowPlayingReturnSelection = 0;
    int m_queueReturnSelection = 0;

    // HOME V10.30 category animation copied into Music so entering the third
    // category uses the same elastic lens motion instead of a bespoke slide.
    float m_homeTabSlide = 2.f;
    float m_homeTabAnimFrom = 2.f;
    float m_homeTabAnimTo = 2.f;
    float m_homeTabAnimTime = 0.f;
    bool m_homeTabAnimating = false;

    // Selected album/playlist metadata fades in independently from the cover
    // carousel, matching the HOME title reveal rhythm after rapid navigation.
    float m_rootInfoReveal = 1.f;

    // Optional four-frame horizontal sprite sheet for the now-playing runner.
    // No Nintendo asset is bundled; a later approved asset can replace the
    // fallback without changing tracklist layout or playback code.
    nxui::Texture m_nowPlayingRunnerTexture;
    bool m_runnerLoadAttempted = false;

    LibrarySnapshot m_library;
    MusicPlaylistStore m_playlistStore;
    MusicClient m_client;
    MusicCoverCache m_coverCache{18};

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

    // HOME <-> Music transition and BGM ownership state.
    float m_transitionAlpha = 0.f;
    bool m_closing = false;
    bool m_lastSessionGuardState = false;

    Modal m_modal = Modal::None;
    std::string m_keyboardText;
    int m_keyboardIndex = 0;
    uint64_t m_pendingPlaylistTrackId = 0;
    int m_modalSelection = 0;

    bool m_touchTracking = false;
    bool m_touchTimelineScrub = false;
    float m_touchStartX = 0.f;
    float m_touchStartY = 0.f;
};

} // namespace switchu::menu::music
