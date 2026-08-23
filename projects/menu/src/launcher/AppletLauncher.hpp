#pragma once
#include <functional>
#include <atomic>
#include <utility>
#include <switch.h>

class AppletLauncher {
public:
    struct Callbacks {
        std::function<void()> playSfxModalHide;
        std::function<void()> requestExit;
    };

    using SpecialLaunchHandler = std::function<bool(uint64_t)>;

    void init(Callbacks cbs);
    void setSpecialLaunchHandler(SpecialLaunchHandler cb) { m_specialLaunchHandler = std::move(cb); }

    void launchAlbum();
    void launchMiiEditor();
    void launchControllerPairing();
    void launchNetConnect();
    void launchSystemSettings();
    void launchUserPage(AccountUid uid);
    void enterSleep();
    void shutdown();
    void reboot();

    void launchApplication(uint64_t titleId, AccountUid uid);
    Result resumeApplication();
    void terminateApplication();

    void checkRunningApplication();

    bool isAppRunning() const;
    bool isAppSuspended(uint64_t titleId) const;
    uint64_t suspendedTitleId() const;

    void setAppRunning(bool v);
    void setAppHasForeground(bool v);
    void setSuspendedTitleId(uint64_t v);

#ifdef SWITCHU_MENU
    // ApplicationSuspended/ApplicationExited update the suspended title just
    // before they enqueue HomeButton. This short-lived marker lets
    // SystemMessages distinguish those required state transitions from a
    // physical HOME press that should be ignored while Switch U is active.
    static bool consumeRecentApplicationStateChange(uint64_t maxAgeMs = 250);
    void setStartupStatus(uint64_t suspendedTitleId, bool appRunning);
#endif

private:
#ifdef SWITCHU_MENU
    std::atomic<bool> m_appRunning{false};
    std::atomic<bool> m_appHasForeground{false};
    std::atomic<uint64_t> m_suspendedTitleId{0};
    static std::atomic<uint64_t> s_lastApplicationStateChangeMs;
#endif

    Callbacks m_cb;
    SpecialLaunchHandler m_specialLaunchHandler;
};
