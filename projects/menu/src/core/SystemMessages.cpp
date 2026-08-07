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
        // V7.4: ApplicationSuspended/ApplicationExited ont maintenant leurs
        // propres actions. Un HomeRequest recu alors que Switch U est deja
        // actif est donc uniquement un HOME physique redondant.
        if (a == SysAction::HomeButton) {
            DebugLog::log("[pump] physical HomeButton ignored while Switch U is active (lock=%d)",
                          LockScreenView::isVisiblyActive() ? 1 : 0);
            continue;
        }
        m_callback(a);
    }
}

void SystemMessages::start() {
}

void SystemMessages::stop() {
}
#endif
