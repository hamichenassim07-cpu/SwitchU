#include "SystemMessages.hpp"
#include "DebugLog.hpp"
#include "widgets/LockScreenView.hpp"
#include "launcher/AppletLauncher.hpp"
#include <switch.h>

#ifdef SWITCHU_MENU

void SystemMessages::pushAction(SysAction a) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_pending.push_back(a);
}

void SystemMessages::pump() {
    std::vector<SysAction> actions;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        actions.swap(m_pending);
    }
    if (!m_callback)
        return;

    for (auto a : actions) {
        if (a == SysAction::HomeButton) {
            const bool requiredStateChange =
                AppletLauncher::consumeRecentApplicationStateChange();

            // Physical HOME presses are redundant while Switch U already owns
            // the screen, both on HOME and on the lockscreen. Only the action
            // paired with a fresh ApplicationSuspended/ApplicationExited
            // state update is allowed through.
            if (!requiredStateChange) {
                DebugLog::log("[pump] physical HomeButton ignored while Switch U is active (lock=%d)",
                              LockScreenView::isVisiblyActive() ? 1 : 0);
                continue;
            }
        }
        m_callback(a);
    }
}

void SystemMessages::start() {
}

void SystemMessages::stop() {
}
#endif
