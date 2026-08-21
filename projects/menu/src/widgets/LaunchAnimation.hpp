#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/widgets/GlassPanel.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/core/Texture.hpp>
#include <switch/services/acc.h>
#include <functional>

using LaunchCallback = std::function<void(uint64_t titleId, AccountUid uid)>;

class LaunchAnimation : public nxui::Widget {
public:
    LaunchAnimation() = default;
    void start(const nxui::Rect& from, const nxui::Texture* tex, float cornerRadius,
               const nxui::Color& panelColor, const nxui::Color& borderColor,
               uint64_t titleId, AccountUid uid,
               LaunchCallback onLaunch = {}, nxui::VoidCallback onDone = {});

    bool isPlaying() const { return m_playing; }
    void stop();

    // V10.20: lightweight global launch state used only by HOME presentation
    // widgets. It lets the carousel hide the source cover and lets the lower
    // title/actions leave the screen without changing the launch callback API.
    static bool globalPlaying();
    static float globalHudExitProgress();

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    float hudExitProgress() const;
    void clearGlobalStateIfOwned();

    bool  m_playing  = false;
    float m_timer    = 0.f;

    // V10.21: faster flip than V10.20 while keeping the full object readable.
    static constexpr float kFormDur        = 0.22f;
    static constexpr float kSpinDur        = 0.54f;
    static constexpr float kSettleDur      = 0.12f;
    static constexpr float kInsertDur      = 0.56f;
    static constexpr float kWipeDur        = 0.34f;
    static constexpr float kBlackHoldDur   = 0.14f;
    static constexpr float kPostLaunchBlackHold = 0.22f;

    static constexpr float kSpinStart      = kFormDur;
    static constexpr float kSettleStart    = kSpinStart + kSpinDur;
    static constexpr float kInsertStart    = kSettleStart + kSettleDur;
    static constexpr float kWipeStart      = kInsertStart + kInsertDur;
    static constexpr float kLaunchMoment   = kWipeStart + kWipeDur;
    static constexpr float kTotalDur       = kLaunchMoment + kBlackHoldDur;

    // Lower title/actions clear the insertion path before the cartridge drops.
    static constexpr float kHudExitStart   = kSpinStart + kSpinDur * 0.46f;
    static constexpr float kHudExitDur     = 0.28f;

    nxui::Rect     m_from;
    nxui::Rect     m_target;
    float          m_cornerRadius = 24.f;
    const nxui::Texture* m_tex = nullptr;
    nxui::Color    m_panelColor;
    nxui::Color    m_borderColor;
    uint64_t       m_titleId = 0;
    AccountUid     m_uid = {};
    LaunchCallback m_onLaunch;
    nxui::VoidCallback m_onDone;
    bool           m_launched = false;
    bool           m_doneCalled = false;
    bool           m_postLaunchHold = false;
    bool           m_insertSfxPlayed = false;

    static LaunchAnimation* s_activeInstance;
};
