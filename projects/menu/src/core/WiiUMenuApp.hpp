#pragma once
#include <nxui/Activity.hpp>
#include <nxui/Application.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/Theme.hpp>
#include "widgets/IconGrid.hpp"
#include "core/GridModel.hpp"
#include "widgets/SelectionCursor.hpp"
#include "widgets/WaraWaraBackground.hpp"
#include "widgets/DateTimeWidget.hpp"
#include "widgets/BatteryWidget.hpp"
#include "widgets/TitlePillWidget.hpp"
#include "widgets/ProfileTitlePillWidget.hpp"
#include "widgets/GameActionsHudWidget.hpp"
#include "widgets/CircularSelectionHaloWidget.hpp"
#include "core/AudioManager.hpp"
#include "core/AccessibilityManager.hpp"
#include "widgets/LaunchAnimation.hpp"
#include "widgets/OverlayDialog.hpp"
#include "widgets/ProgressDialog.hpp"
#include "widgets/AppletButton.hpp"
#include "widgets/PageIndicator.hpp"
#include "widgets/LockScreenView.hpp"
#include "widgets/UserAvatarButton.hpp"
#include "settings/SettingsScreen.hpp"
#include "themeshop/ThemeShopScreen.hpp"
#include "core/Config.hpp"
#include "core/ThemePreset.hpp"
#include "sidebar/SidebarManager.hpp"
#include "launcher/AppletLauncher.hpp"
#include "launcher/AppListLoader.hpp"
#include "launcher/IconStreamer.hpp"
#include "core/SystemMessages.hpp"
#ifdef SWITCHU_DEBUG_UI
#include "debug/DebugImGuiOverlay.hpp"
#endif
#include <nxui/widgets/Background.hpp>
#include <nxui/widgets/Box.hpp>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <future>
#include <switch.h>
#include <switchu/smi_protocol.hpp>

// V7.4.3 direct: les anciennes implementations restees dans les fichiers
// historiques sont marquees weak. Le fichier WiiUMenuAppRoutingV74.cpp
// fournit les implementations fortes du routage lockscreen. Ainsi, aucune
// etape .bat/.ps1 n'est necessaire avant la compilation.
#if defined(__GNUC__) && !defined(SWITCHU_V74_ROUTING_STRONG)
#define SWITCHU_V74_LEGACY_WEAK __attribute__((weak))
#else
#define SWITCHU_V74_LEGACY_WEAK
#endif

#ifdef SWITCHU_HOMEBREW
static constexpr const char* SD_ASSETS = "romfs:";
#else
static constexpr const char* SD_ASSETS = "sdmc:/switch/SwitchU";
#endif

class WiiUMenuApp : public nxui::Activity {
public:
    WiiUMenuApp();
    ~WiiUMenuApp();

    void setTutorialStartupFade(bool enabled);

#ifdef SWITCHU_MENU
    // Signature historique conservee uniquement pour que l'ancien corps
    // present dans WiiUMenuApp.cpp compile. Elle est weak et n'est plus
    // utilisee par le demarrage V7.4.3.
    void setStartupStatus(uint64_t suspendedTitleId,
                          bool appRunning) SWITCHU_V74_LEGACY_WEAK;

    // V7.4.3 : vraie entree de demarrage, avec la destination memorisee
    // par le daemon avant la veille.
    void setStartupStatus(uint64_t suspendedTitleId,
                          bool appRunning,
                          switchu::smi::LockReturnTarget returnTarget);

    // V6.5.1: HOME opens Switch U's HOME directly, while boot/wakeup keeps
    // the lockscreen. The daemon already passes MainMenu for a physical HOME
    // press and StartupBoot for boot/wakeup.
    void setStartupMode(switchu::smi::MenuStartMode mode) {
        m_skipStartupLock = (mode == switchu::smi::MenuStartMode::MainMenu);
    }
#endif

    bool onCreate() override;
    void onDestroy() override;
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

    nxui::Widget* focusRoot() override;

private:
    struct GridLayoutMetrics {
        float cellW = 150.f;
        float cellH = 150.f;
        float padX = 20.f;
        float padY = 16.f;
    };

    void loadResources();
    GridLayoutMetrics computeGridLayoutMetrics() const;
    void reflowHomeGrid();
    void buildGrid();
    void buildUserAvatarBar();
    void applyTheme();
    void applyThemeResources(const ThemePreset& preset);
    void applyUiLanguage();
    void rebuildThemeFromColors();
    ThemePreset buildEffectiveThemePreset();
    std::string resolveThemeAssetPath(const ThemePreset& preset, const std::string& rawPath) const;
    ThemePreset* findPresetPtr(const std::string& name);
    void deletePreset(const std::string& presetId);
    void updateCursor();
    struct ActionHint {
        std::string icon;
        std::string label;
    };
    std::vector<ActionHint> buildActionHints();
    void renderActionHintBar(nxui::Renderer& ren);
    int findTitleIndex(uint64_t titleId) const;
    bool focusTitle(uint64_t titleId);
    void markSuspendedIcon(uint64_t titleId);
    void closeActiveOverlays();
    void handleTouch();

    void showLockScreen();
    void handleLockScreen(float dt) SWITCHU_V74_LEGACY_WEAK;
    void renderLockScreen(nxui::Renderer& ren);
    void prepareLockScreenView();
    void rememberLaunchUser(AccountUid uid);
    std::string resolveLockProfileName(bool hasSuspendedGame) const;
    std::string buildAdaptiveLockGreeting(bool hasSuspendedGame,
                                          const std::string& profileName,
                                          const std::string& gameTitle) const;
    std::shared_ptr<GlossyIcon> makeIcon(const AppEntry& entry);
    void wireFocusCallback();
    void wireGlobalActions();
    void setHomeApplicationsCategory(bool applications);
    void toggleAccessibilitySpeech();
    bool handleAccessibilityToggleCombo();
    bool isCurrentFocusableWidget(nxui::Widget* w) const;
    std::string accessibilityPositionFor(nxui::Widget* w) const;
    void createSettings();
    void createThemeShop();
    void reloadThemePresets();
    void refreshThemeShopState();
    std::vector<ThemeShopScreen::ThemeShopEntry> buildThemeShopEntries();
    void startThemePackageTransfer(const ThemeCatalogClient::Entry& entry, bool installMode);
    void syncThemePackageTransfer();
    void activateThemePreset(ThemePreset* preset, bool applyBundledSound);
    std::string resolveSoundPresetId(const std::string& preset) const;
    void loadSoundPreset(const std::string& preset);
    void changeSoundPreset(const std::string& preset);
    std::vector<std::string> scanAvailablePresets();
    void loadMenuLayout();
    void saveMenuLayout();
    void applyMenuLayoutToPending(std::vector<PendingApp>& apps);
    void startEditGhost(GlossyIcon* sourceIcon);
    void stopEditGhost();
    void updateEditGhost(float dt);
    bool commitEditModePlacement();
    bool moveFocusedIcon(nxui::FocusDirection dir);
    void enterEditMode();
    void exitEditMode();
    void bindEditActions(GlossyIcon* icon);
    void unbindEditActions();
    bool isEditableIcon(nxui::Widget* w) const;
    void announceFocusedWidget(nxui::Widget* w);
    std::string accessibilityContextFor(nxui::Widget* w) const;
    std::string accessibilityActionsFor(nxui::Widget* w) const;

#ifdef SWITCHU_MENU
    void refreshAppList();
    void finalizeRefresh();
    void handleSystemAction(SysAction a) SWITCHU_V74_LEGACY_WEAK;
#endif

    nxui::Font  m_fontNormal;
    nxui::Font  m_fontSmall;
    nxui::Font  m_fontLockLarge;
    nxui::Font  m_fontLockMedium;
    nxui::Font  m_fontIcons;

    GridModel    m_model;
    nxui::Theme  m_theme;

    std::string              m_activePresetName = "Default Light";
    ThemeColorSet            m_activeColors;
    nxui::ThemeMode          m_activeMode = nxui::ThemeMode::Light;
    std::vector<ThemePreset> m_allPresets;
    ThemePreset              m_effectivePreset;

    std::shared_ptr<WaraWaraBackground> m_background;
    std::shared_ptr<IconGrid>          m_grid;
    std::shared_ptr<SelectionCursor>   m_cursor;
    std::shared_ptr<SelectionCursor>   m_pointerCursor;
    std::shared_ptr<DateTimeWidget>    m_clock;
    std::shared_ptr<BatteryWidget>     m_battery;
    std::shared_ptr<TitlePillWidget>   m_titlePill;
    std::shared_ptr<ProfileTitlePillWidget> m_profileTitlePill;
    std::shared_ptr<GameActionsHudWidget> m_gameActionsHud;
    std::shared_ptr<CircularSelectionHaloWidget> m_systemSelectionHalo;
    std::shared_ptr<PageIndicator>     m_pageIndicator;
    std::shared_ptr<LockScreenView>    m_lockScreenView;
    std::shared_ptr<LaunchAnimation>   m_launchAnim;
    std::shared_ptr<OverlayDialog>     m_userSelect;
    std::shared_ptr<OverlayDialog>     m_dialog;
    std::shared_ptr<ProgressDialog>    m_progressDialog;
    std::shared_ptr<SettingsScreen>    m_settings;
    std::shared_ptr<ThemeShopScreen>   m_themeShop;

    nxui::Texture m_gameCardTex;

    std::shared_ptr<nxui::Box> m_bgLayer;
    std::shared_ptr<nxui::Box> m_contentLayer;
    std::shared_ptr<nxui::Box> m_overlayLayer;
    std::shared_ptr<nxui::Box> m_topHud;
    std::shared_ptr<nxui::Box> m_leftSidebar;
    std::shared_ptr<nxui::Box> m_rightSidebar;
    std::shared_ptr<nxui::Box> m_userAvatarBar;
    std::vector<std::shared_ptr<UserAvatarButton>> m_userAvatarButtons;
    bool m_v107HudPositioned = false;
    float m_profilePillAnchorWidth = 1280.f;

    AudioManager m_audio;
    AccessibilityManager m_accessibility;
    std::future<void>    m_audioFuture;
    bool                 m_audioStarted = false;
    std::vector<std::string> m_availablePresets;
    bool                 m_presetChangePending = false;
    std::string          m_loadedSoundPreset;
    std::string          m_pendingSoundPreset;

    struct ThemePackageTransferShared {
        std::mutex mutex;
        ThemeTransferState state;
        std::string themeId;
        bool installMode = false;
        std::string destinationPath;
        std::uint64_t revision = 0;
    };

    nxui::ThreadPool m_threadPool{2};
    SidebarManager  m_sidebar;
    AppletLauncher  m_launcher;
    AppListLoader   m_appLoader;
    IconStreamer    m_iconStreamer;
    SystemMessages  m_sysMsg;

    bool m_showDebugOverlay  = false;
#ifdef SWITCHU_DEBUG_UI
    std::unique_ptr<DebugImGuiOverlay> m_debugOverlay;
#endif
    bool m_showWireframe     = false;
    bool m_editMode          = false;
    int  m_editSourceIndex   = -1;
    std::string m_editHeldTitle;
    GlossyIcon* m_editBoundIcon = nullptr;
    GlossyIcon* m_editSourceIcon = nullptr;
    std::shared_ptr<GlossyIcon> m_editGhostIcon;
    nxui::Rect m_editGhostTargetRect {0.f, 0.f, 0.f, 0.f};
    float m_editGhostPulse = 0.f;
    std::vector<uint64_t> m_layoutSlots;
    bool m_layoutDirty = false;

    int  m_touchHitIndex     = -1;
    bool m_touchOnFocused    = false;
    bool m_touchEditDragActive = false;

    // Scroll tactile de la rangée d'applications.
    bool  m_touchStartedInGrid = false;
    bool  m_touchScrollActive = false;
    float m_touchLastX = 0.f;
    float m_touchLastDuration = 0.f;
    float m_touchScrollVelocity = 0.f;

    UserAvatarButton* m_touchAvatarTarget = nullptr;
    bool m_touchAvatarWasFocused = false;

    bool  m_lockScreenActive = true;
    bool  m_lockScreenUnlocking = false;
    bool  m_skipStartupLock = false;
    // V7.4 : destination explicite transmise par le daemon.
    // Elle represente l'ecran qui etait actif AVANT la mise en veille.
    switchu::smi::LockReturnTarget m_lockReturnTarget =
        switchu::smi::LockReturnTarget::Home;
    int   m_lockPressCount = 0;
    float m_lockPressResetTimer = 0.f;
    float m_lockScreenPulse = 0.f;
    float m_lockScreenOpacity = 1.f;
    float m_lockScreenReveal = 0.f;
    float m_lockUnlockProgress = 0.f;
    float m_lockPressFlash = 0.f;
    float m_lockBatteryPollTimer = 0.f;
    uint32_t m_lockBatteryPercent = 100;
    bool m_lockBatteryCharging = false;
    int m_lockGreetingIndex = 0;
    int m_lockPinnedIconIndex = -1;
    AccountUid m_lastLaunchUid = {};
    bool m_lastLaunchUidValid = false;
    std::string m_lastLaunchProfileName;

    int  m_deferredRefreshFrames = 0;
    bool m_refreshQueued         = false;
    int  m_refreshCooldownFrames = 0;
    bool m_asyncRefreshPending   = false;
    int  m_refreshPrevPage       = 0;

    AppConfig m_config;
    bool m_settingsNeedRefresh        = false;
    std::string m_loadedRegularFontPath;
    std::string m_loadedSmallFontPath;
    std::string m_loadedGameCardPath;
    std::string m_loadedBackgroundImagePath;
    bool m_backgroundImageLoaded      = false;
    bool m_forceThemeResourceReload   = false;
    nxui::Widget* m_dialogReturnFocus = nullptr;
    bool m_dialogWasActive            = false;
    bool m_suppressNextNavigateSfx    = false;
    bool m_pendingNetConnect          = false;
    int  m_deferredBluetoothInitFrames = 0;
    int  m_deferredInitialAssetFrames = 0;
    std::future<void> m_themePackageTransferFuture;
    std::shared_ptr<ThemePackageTransferShared> m_themePackageTransfer;
    std::uint64_t m_themePackageTransferUiRevision = 0;
    std::uint64_t m_themePackageTransferHandledRevision = 0;
    int m_themeRenderDebugFrames = 0;

    float m_returnFadeTimer = 0.f;
    float m_tutorialStartupFadeTimer = 0.f;
    bool  m_tutorialStartupFade = false;
    bool m_hintPanelInitialized = false;
    bool m_accessibilityToggleComboHeld = false;
    bool m_plusExitPending = false;
    float m_plusExitPendingTimer = 0.f;
    nxui::AnimatedFloat m_hintPanelW{0.f};
    nxui::AnimatedFloat m_hintPanelH{0.f};
    nxui::AnimatedFloat m_hintContentReveal{1.f};
    std::string m_hintSignature;
    static constexpr float kReturnFadeInDur = 0.22f;
    static constexpr float kTutorialStartupFadeDur = 0.34f;
};
