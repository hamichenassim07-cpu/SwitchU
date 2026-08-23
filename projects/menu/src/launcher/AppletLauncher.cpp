#include "AppletLauncher.hpp"
#include "core/DebugLog.hpp"
#include <nxui/Application.hpp>
#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#include <switchu/smi_protocol.hpp>
#endif
#include <switch.h>
#include <chrono>

void AppletLauncher::init(Callbacks cbs) {
    m_cb = std::move(cbs);
}

#ifdef SWITCHU_MENU

std::atomic<uint64_t> AppletLauncher::s_lastApplicationStateChangeMs{0};

namespace {
uint64_t launcherMonotonicMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

bool AppletLauncher::isAppRunning() const { return m_appRunning; }
bool AppletLauncher::isAppSuspended(uint64_t titleId) const {
    return m_suspendedTitleId != 0 && m_suspendedTitleId == titleId;
}
uint64_t AppletLauncher::suspendedTitleId() const { return m_suspendedTitleId; }

void AppletLauncher::setAppRunning(bool v) { m_appRunning = v; }
void AppletLauncher::setAppHasForeground(bool v) { m_appHasForeground = v; }
void AppletLauncher::setSuspendedTitleId(uint64_t v) {
    m_suspendedTitleId = v;
    s_lastApplicationStateChangeMs.store(launcherMonotonicMs());
}

bool AppletLauncher::consumeRecentApplicationStateChange(uint64_t maxAgeMs) {
    const uint64_t stamp = s_lastApplicationStateChangeMs.exchange(0);
    if (stamp == 0)
        return false;
    const uint64_t now = launcherMonotonicMs();
    return now >= stamp && (now - stamp) <= maxAgeMs;
}

void AppletLauncher::setStartupStatus(uint64_t suspendedTitleId, bool appRunning) {
    m_suspendedTitleId = suspendedTitleId;
    m_appRunning = appRunning;
    m_appHasForeground = false;
    DebugLog::log("[launcher] startup status: suspended=0x%016lX running=%d",
                  suspendedTitleId, appRunning);
}

void AppletLauncher::launchAlbum() {
    DebugLog::log("[launcher] requesting Album launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(
        switchu::smi::SystemMessage::LaunchAlbum
    );
    DebugLog::log("[launcher] Album rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit) m_cb.requestExit();
    }
}

void AppletLauncher::launchMiiEditor() {
    DebugLog::log("[launcher] requesting Mii Editor launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(
        switchu::smi::SystemMessage::LaunchMiiEditor
    );
    DebugLog::log("[launcher] Mii Editor rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit) m_cb.requestExit();
    }
}

void AppletLauncher::launchControllerPairing() {
    DebugLog::log("[launcher] requesting Controller pairing via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(
        switchu::smi::SystemMessage::LaunchControllers
    );
    DebugLog::log("[launcher] Controller pairing rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit) m_cb.requestExit();
    }
}

void AppletLauncher::launchNetConnect() {
    DebugLog::log("[launcher] requesting NetConnect launch via daemon");
    Result rc = switchu::menu::smi_cmd::sendSimple(
        switchu::smi::SystemMessage::LaunchNetConnect
    );
    DebugLog::log("[launcher] NetConnect rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit) m_cb.requestExit();
    }
}

void AppletLauncher::launchSystemSettings() {
    DebugLog::log("[launcher] V10.29 requesting Nintendo System Settings via daemon");
    Result rc = switchu::menu::smi_cmd::launchSystemSettings();
    DebugLog::log("[launcher] Nintendo System Settings command rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        // The daemon must own the actual applet creation attempt. Closing the
        // external HOME menu releases the foreground LibraryApplet slot first.
        if (m_cb.requestExit) m_cb.requestExit();
    }
}

void AppletLauncher::launchUserPage(AccountUid uid) {
    DebugLog::log("[launcher] requesting User Page launch via daemon");
    Result rc = switchu::menu::smi_cmd::launchUserPage(uid);
    DebugLog::log("[launcher] User Page rc=0x%X", rc);
    if (R_SUCCEEDED(rc)) {
        if (m_cb.playSfxModalHide) m_cb.playSfxModalHide();
        if (m_cb.requestExit) m_cb.requestExit();
    }
}

void AppletLauncher::enterSleep() {
    DebugLog::log("[launcher] requesting sleep");
    switchu::menu::smi_cmd::enterSleep();
}

void AppletLauncher::shutdown() {
    DebugLog::log("[launcher] requesting shutdown");
    switchu::menu::smi_cmd::shutdown();
}

void AppletLauncher::reboot() {
    DebugLog::log("[launcher] requesting reboot");
    switchu::menu::smi_cmd::reboot();
}

void AppletLauncher::launchApplication(uint64_t titleId, AccountUid uid) {
    DebugLog::log("[launcher] tid=%016lX", titleId);
    if (m_specialLaunchHandler && m_specialLaunchHandler(titleId)) {
        DebugLog::log("[launcher] special HOME card handled locally: %016lX", titleId);
        return;
    }
    Result rc = switchu::menu::smi_cmd::launchApplication(titleId, uid);
    if (R_FAILED(rc)) {
        DebugLog::log("[launcher] FAIL: 0x%X", rc);
        return;
    }
    DebugLog::log("[launcher] command sent, closing menu");
    if (m_cb.requestExit) m_cb.requestExit();
}

Result AppletLauncher::resumeApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended!");
        return 1;
    }

    DebugLog::log("[launcher] requesting resume");
    const Result rc = switchu::menu::smi_cmd::resumeApplication();
    DebugLog::log("[launcher] resume rc=0x%X", rc);
    if (R_FAILED(rc))
        return rc;

    DebugLog::log("[launcher] resume accepted, closing menu");
    if (auto* application = nxui::Application::current()) {
        DebugLog::log("[launcher] waiting for GPU before menu exit");
        application->gpu().waitIdle();
    }
    if (m_cb.requestExit) m_cb.requestExit();
    return rc;
}

void AppletLauncher::terminateApplication() {
    if (m_suspendedTitleId == 0) {
        DebugLog::log("[launcher] no app suspended, nothing to terminate");
        return;
    }
    DebugLog::log("[launcher] requesting terminate 0x%016lX",
                  static_cast<uint64_t>(m_suspendedTitleId));
    switchu::menu::smi_cmd::terminateApplication();
}

void AppletLauncher::checkRunningApplication() {}

#else

bool AppletLauncher::isAppRunning() const { return false; }
bool AppletLauncher::isAppSuspended(uint64_t) const { return false; }
uint64_t AppletLauncher::suspendedTitleId() const { return 0; }
void AppletLauncher::setAppRunning(bool) {}
void AppletLauncher::setAppHasForeground(bool) {}
void AppletLauncher::setSuspendedTitleId(uint64_t) {}

void AppletLauncher::launchAlbum() {}
void AppletLauncher::launchMiiEditor() {}
void AppletLauncher::launchControllerPairing() {}
void AppletLauncher::launchNetConnect() {}
void AppletLauncher::launchSystemSettings() {}
void AppletLauncher::launchUserPage(AccountUid) {}
void AppletLauncher::enterSleep() {}
void AppletLauncher::shutdown() {}
void AppletLauncher::reboot() {}
void AppletLauncher::launchApplication(uint64_t titleId, AccountUid) {
    if (m_specialLaunchHandler)
        m_specialLaunchHandler(titleId);
}
Result AppletLauncher::resumeApplication() { return 0; }
void AppletLauncher::terminateApplication() {}
void AppletLauncher::checkRunningApplication() {}

#endif
