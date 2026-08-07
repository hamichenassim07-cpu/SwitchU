#pragma once

#include <nxui/widgets/Widget.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/Theme.hpp>

#include "LockPressIndicator.hpp"

#include <cstdint>
#include <atomic>
#include <string>

class LockScreenView : public nxui::Widget {
public:
    LockScreenView();

    void setFonts(nxui::Font* normal,
                  nxui::Font* small,
                  nxui::Font* large,
                  nxui::Font* medium,
                  nxui::Font* icons);
    void setTheme(const nxui::Theme* theme);
    void setUse12HourClock(bool enabled) { m_use12Hour = enabled; }
    void setGameCardTexture(nxui::Texture* texture) { m_gameCardTexture = texture; }

    void setSuspendedGame(nxui::Texture* texture,
                          const std::string& title,
                          std::uint64_t titleId,
                          bool gameCard);
    void clearSuspendedGame();
    bool hasSuspendedGame() const { return m_hasGame; }
    static bool isVisiblyActive() { return s_visiblyActive.load(); }

    void setGreeting(const std::string& greeting);
    void setBatteryStatus(std::uint32_t percentage, bool charging);
    void setProgress(int progress, float flash);
    void setTransition(float opacity,
                       float reveal,
                       float unlockProgress,
                       float pulse,
                       bool unlocking);

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    static std::atomic<bool> s_visiblyActive;

    void applyThemeToWidgets();
    void ensureDynamicAssets(nxui::Renderer& ren);
    void ensureBackground(nxui::Renderer& ren);
    void ensureProfile(nxui::Renderer& ren);
    void resetBackgroundAsset();
    void resetProfileAsset();
    void updateStorageStatus();

    nxui::Font* m_fontNormal = nullptr;
    nxui::Font* m_fontSmall = nullptr;
    nxui::Font* m_fontLarge = nullptr;
    nxui::Font* m_fontMedium = nullptr;
    nxui::Font* m_fontIcons = nullptr;
    const nxui::Theme* m_theme = nullptr;

    nxui::GlassPanel m_profilePanel;
    nxui::GlassPanel m_connectionPanel;
    nxui::GlassPanel m_storagePanel;
    LockPressIndicator m_progress;

    nxui::Texture* m_gameTexture = nullptr;
    nxui::Texture m_ownedGameTexture;
    std::uint64_t m_ownedGameTextureTitleId = 0;
    bool m_ownedGameTextureAttempted = false;
    bool m_deferredGameAssetReset = false;
    nxui::Texture* m_gameCardTexture = nullptr;
    std::string m_gameTitle;
    std::uint64_t m_gameTitleId = 0;
    bool m_gameIsCard = false;
    bool m_hasGame = false;

    nxui::Texture m_backgroundTexture;
    std::string m_backgroundPath;
    bool m_backgroundAttempted = false;

    nxui::Texture m_profileAvatarTexture;
    std::string m_profileName;
    std::string m_profileNameHint;
    bool m_profileAttempted = false;

    float m_connectionPollTimer = 0.f;
    float m_storagePollTimer = 0.f;
    float m_animationTime = 0.f;
    std::string m_audioStatus = "Haut-parleurs de la console";
    std::string m_controllerStatus = "État indisponible";
    std::uint64_t m_sdTotalBytes = 0;
    std::uint64_t m_sdFreeBytes = 0;
    bool m_storageAvailable = false;

    std::string m_greeting = "Bon retour.";
    bool m_use12Hour = false;
    std::uint32_t m_batteryPercent = 100;
    bool m_batteryCharging = false;
    int m_pressCount = 0;
    float m_pressFlash = 0.f;
    float m_visualPressProgress = 0.f;
    float m_viewOpacity = 1.f;
    float m_reveal = 1.f;
    float m_unlockProgress = 0.f;
    float m_pulse = 0.f;
    bool m_unlocking = false;

    bool m_musicSceneEntered = false;
    bool m_unlockAudioStarted = false;
    bool m_resumeHandoffPrepared = false;
    bool m_resumeBlackFrameRendered = false;
    bool m_resumeHandoffSent = false;
};
