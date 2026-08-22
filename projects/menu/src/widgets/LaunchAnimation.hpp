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

    // V10.25 normal launch timeline. The drop starts 85 ms before the 360°
    // spin ends so movement reads as one continuous physical gesture.
    static constexpr float kFormDur          = 0.28f;
    static constexpr float kSpinDur          = 0.92f;
    static constexpr float kSpinDropOverlap  = 0.085f;
    static constexpr float kDropDur          = 0.50f;
    static constexpr float kPreClickHoldDur  = 0.060f;
    static constexpr float kClickDownDur     = 0.10f;
    static constexpr float kReboundDur       = 0.050f;
    static constexpr float kLockDur          = 0.060f;
    static constexpr float kLockHoldDur      = 0.10f;
    static constexpr float kWipeDur          = 0.26f;
    static constexpr float kBlackHoldDur     = 0.14f;
    static constexpr float kPostLaunchBlackHold = 0.24f;

    static constexpr float kSpinStart        = kFormDur;
    static constexpr float kSpinEnd          = kSpinStart + kSpinDur;
    static constexpr float kDropStart        = kSpinEnd - kSpinDropOverlap;
    static constexpr float kDropEnd          = kDropStart + kDropDur;
    static constexpr float kPreClickHoldStart = kDropEnd;
    static constexpr float kClickStart       = kPreClickHoldStart + kPreClickHoldDur;
    static constexpr float kReboundStart     = kClickStart + kClickDownDur;
    static constexpr float kLockStart        = kReboundStart + kReboundDur;
    static constexpr float kLockHoldStart    = kLockStart + kLockDur;
    static constexpr float kWipeStart        = kLockHoldStart + kLockHoldDur;
    static constexpr float kLaunchMoment     = kWipeStart + kWipeDur;
    static constexpr float kTotalDur         = kLaunchMoment + kBlackHoldDur;

    // Resume path: no insertion, no click. Stabilise -> wake pulse -> small
    // approach -> black handoff.
    static constexpr float kResumeAnimDur    = 0.42f;
    static constexpr float kResumeWipeDur    = 0.22f;
    static constexpr float kResumeLaunchMoment = kResumeAnimDur + kResumeWipeDur;
    static constexpr float kResumeTotalDur   = kResumeLaunchMoment + 0.14f;

    static constexpr float kHudExitStart     = kSpinStart + kSpinDur * 0.70f;
    static constexpr float kHudExitDur       = 0.34f;

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
    bool           m_spinSfxPlayed = false;
    bool           m_resumeSfxPlayed = false;
    bool           m_resumeMode = false;
    bool           m_orderMarked = false;
    uint64_t       m_effectiveTitleId = 0;

    static LaunchAnimation* s_activeInstance;
};
