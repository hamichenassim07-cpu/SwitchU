#pragma once

#include "MusicClient.hpp"
#include "MusicCoverCache.hpp"
#include "MusicLibrary.hpp"
#include "MusicPlaylistStore.hpp"
#include "widgets/TitlePillWidget.hpp"
#include "widgets/DateTimeWidget.hpp"
#include "widgets/HomeCarouselMotion.hpp"
#include "MusicPhysicalMediaRenderer.hpp"
#include "MusicAmbientBackground.hpp"
#include "MusicAlbumDetails.hpp"

#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/Widget.hpp>

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace switchu::menu::music {

class MusicScreen : public nxui::Widget {
public:
    MusicScreen();
    ~MusicScreen() override;

    void setFonts(nxui::Font* normal, nxui::Font* small, nxui::Font* icons) {
        m_font = normal; m_smallFont = small; m_iconFont = icons;
    }
    void setHomeHudWidgets(DateTimeWidget* clock, nxui::Widget* profile,
                           nxui::Widget* battery, TitlePillWidget* titlePill) {
        m_homeClockWidget = clock;
        m_homeProfileWidget = profile;
        m_homeBatteryWidget = battery;
        m_homeTitlePill = titlePill;
    }
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
        AlbumInformation,
    };

    void setupActions();
    void startScan(bool force = false);
    void finishScanIfReady();
    void refreshStatus(bool force = false);
    void setRootCategory(int index);
    int rootCategoryIndex() const;
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
    bool playTrackIds(const std::vector<uint64_t>& ids, int index);
    void updateNowPlayingSelection(int delta);
    void activateNowPlayingControl();
    void seekRelative(int64_t deltaMs);
    void appendToQueue(const std::vector<uint64_t>& ids);
    void openNowPlaying();

    CoverRef playlistCover(size_t playlistIndex) const;
    uint64_t playlistDurationMs(size_t playlistIndex) const;
    uint64_t albumDurationMs(size_t albumIndex) const;
    PhysicalMediaGeometry drawPhysicalMedia(nxui::Renderer& ren, const CoverRef& cover,
                                                const nxui::Rect& rect, bool playlist,
                                                float yawDeg, float pitchDeg, float rollDeg,
                                                float zLiftPx, float vinylReveal,
                                                float vinylSpinRad, float alpha,
                                                bool playing, int maxSide = 512);
    nxui::Color artworkAccent(const CoverRef& cover) const;

    const Track* currentTrack() const;
    const Track* trackForId(uint64_t id) const;
    std::vector<uint64_t> albumTrackIds(size_t albumIndex) const;
    std::vector<uint64_t> playlistTrackIds(size_t playlistIndex) const;
    size_t selectedTrackIndex() const;

    void drawMusicBackground(nxui::Renderer& ren);
    const CoverRef* ambientCover() const;
    void updateAmbientBackground(float dt);
    void drawAlbumInformation(nxui::Renderer& ren, const Album& album, float alpha);
    void drawTopBar(nxui::Renderer& ren);
    void drawAlbums(nxui::Renderer& ren);
    void drawPlaylists(nxui::Renderer& ren);
    void drawAlbumDetail(nxui::Renderer& ren);
    void drawPlaylistDetail(nxui::Renderer& ren);
    void drawNowPlaying(nxui::Renderer& ren);
    void drawQueue(nxui::Renderer& ren);
    void drawBottomHints(nxui::Renderer& ren);
    void drawModal(nxui::Renderer& ren);
    void drawTrackRow(nxui::Renderer& ren, const Track& track, const nxui::Rect& row,
                      bool selected, int ordinal = 0);
    void drawNowPlayingIndicator(nxui::Renderer& ren, const nxui::Rect& row);
    void drawRootCategoryHint(nxui::Renderer& ren);
    void drawPreviousRootCarousel(nxui::Renderer& ren);
    void drawEmpty(nxui::Renderer& ren, const std::string& title,
                   const std::string& detail);

    std::string formatDuration(uint64_t ms) const;
    std::string fitText(nxui::Font* font, const std::string& text,
                        float maxWidth, float scale) const;
    void drawMarqueeOrFit(nxui::Renderer& ren, const std::string& text,
                          const nxui::Rect& clip, float y, float scale,
                          const nxui::Color& color, bool animate,
                          float elapsedOverride = -1.f) const;
    float visibleListStartVisual(size_t count, int rows) const;
    void clampSelectionForView();
    bool contentTransitionBusy() const;
    bool rootView() const;
    int rootItemCount() const;
    void retargetRootCarousel(bool immediate = false);

    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    nxui::Font* m_iconFont = nullptr;
    DateTimeWidget* m_homeClockWidget = nullptr;
    nxui::Widget* m_homeProfileWidget = nullptr;
    nxui::Widget* m_homeBatteryWidget = nullptr;
    TitlePillWidget* m_homeTitlePill = nullptr;
    std::function<void()> m_closeCb;
    std::function<void(bool)> m_sessionGuardCb;

    bool m_active = false;
    View m_view = View::Albums;
    View m_nowPlayingReturnView = View::Albums;
    View m_queueReturnView = View::NowPlaying;
    int m_selection = 0;
    size_t m_detailAlbum = 0;
    size_t m_detailPlaylist = 0;
    int m_nowControl = 2;

    // The exact HOME carousel motion engine is shared with IconGrid.
    switchu::homeui::HomeCarouselMotionState m_rootCarouselMotion{};
    float m_listVisualSelection = 0.f;
    float m_detailTransition = 1.f;
    bool m_detailClosing = false;
    View m_detailReturnView = View::Albums;
    float m_nowPlayingEnter = 1.f;
    bool m_nowPlayingClosing = false;
    int m_nowPlayingReturnSelection = 0;
    int m_queueReturnSelection = 0;


    // Selected album/playlist metadata fades in independently from the cover
    // carousel, matching the HOME title reveal rhythm after rapid navigation.
    float m_rootInfoReveal = 1.f;

    // Albums <-> Playlists is a hidden secondary category. D-pad Up/Down
    // triggers a short vertical content transition and a temporary label; no
    // permanent category bar is rendered.
    float m_rootCategoryTransition = 1.f;
    int m_rootCategoryDirection = 1;
    float m_rootCategoryHintTimer = 0.f;
    View m_rootCategoryPreviousView = View::Albums;
    float m_rootCategoryPreviousPosition = 0.f;
    int m_rootCategoryPreviousSelection = 0;

    // Exact HOME entry bounce shared with IconGrid. It runs once when Music
    // receives the carousel, never on every left/right selection change.
    bool m_rootEntryBounceActive = false;
    float m_rootEntryBounceTime = 0.f;

    LibrarySnapshot m_library;
    MusicPlaylistStore m_playlistStore;
    MusicClient m_client;
    MusicCoverCache m_coverCache{18};

    // SAFE-ENTRY hotfix: restore the V5/V7 std::async scan ownership model.
    // This keeps metadata parsing in a joinable task instead of a detached worker.
    std::future<LibrarySnapshot> m_scanFuture;
    std::atomic<uint32_t> m_filesVisited{0};
    std::atomic<uint32_t> m_tracksFound{0};
    bool m_scanRunning = false;
    bool m_hasScanned = false;

    switchu::music::Status m_status{};
    float m_statusTimer = 0.f;
    float m_hiddenGuardTimer = 0.f;
    float m_uiTime = 0.f;
    float m_idleTime = 0.f;
    // Compatibility argument at protected carousel call sites; no disc state,
    // spin update, geometry, texture, or label survives in the renderer.
    static constexpr float m_vinylSpinPhase = 0.f;
    float m_sceneParallaxX = 0.f;
    float m_sceneParallaxY = 0.f;
    uint64_t m_marqueeTrackId = 0;
    float m_marqueeElapsed = 0.f;
    // Album-title marquee owns its own clock. Track selection changes must not
    // restart the album name animation.
    float m_albumTitleMarqueeElapsed = 0.f;
    nxui::Texture m_homePlayTimeClockTexture;
    bool m_homePlayTimeClockLoadAttempted = false;
    MusicAmbientBackground m_ambientBackground;
    const CoverRef* m_ambientCoverRef = nullptr;
    uint64_t m_ambientScanGeneration = 0;
    std::string m_ambientPaletteKey;

    struct AlbumInformation {
        const Album* album = nullptr;
        const nxui::Font* font = nullptr;
        uint64_t generation = 0, fontRevision = 0;
        std::string title, artist, duration, count;
        float titleScale = 1.f, artistScale = 1.f, metaScale = 1.f;
        float titleWidth = 0.f, artistWidth = 0.f;
        float countWidth = 0.f, durationWidth = 0.f;
        float durationHeight = 0.f, countHeight = 0.f;
    } m_albumInformation;
    bool m_nextSoonWasVisible = false;
    float m_nextToastTimer = 0.f;

    // HOME <-> Music transition and BGM ownership state.
    float m_transitionAlpha = 0.f;
    bool m_closing = false;
    bool m_lastSessionGuardState = false;

    MusicAlbumDetails m_albumDetails;
    Modal m_modal = Modal::None;
    std::string m_keyboardText;
    int m_keyboardIndex = 0;
    uint64_t m_pendingPlaylistTrackId = 0;
    int m_modalSelection = 0;

    bool m_touchTracking = false;
    bool m_touchTimelineScrub = false;
    bool m_touchStartedInCarousel = false;
    bool m_touchScrollActive = false;
    float m_touchStartX = 0.f;
    float m_touchStartY = 0.f;
    float m_touchLastX = 0.f;
    float m_touchLastDuration = 0.f;
    float m_touchScrollVelocity = 0.f;
};

} // namespace switchu::menu::music
