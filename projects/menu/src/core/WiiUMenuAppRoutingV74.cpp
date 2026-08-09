// Switch U V7.4.3 - routage lockscreen directement compile.
// Aucun script d'injection n'est requis.
#define SWITCHU_V74_ROUTING_STRONG 1
#include "WiiUMenuApp.hpp"
#include "DebugLog.hpp"

#ifdef SWITCHU_MENU
#include "smi_commands.hpp"
#endif

#include <algorithm>

namespace {

float v74Clamp01(float value) {
    return std::clamp(value, 0.f, 1.f);
}

float v74SmoothStep(float value) {
    value = v74Clamp01(value);
    return value * value * (3.f - 2.f * value);
}

} // namespace

#ifdef SWITCHU_MENU
void WiiUMenuApp::setStartupStatus(
    uint64_t suspendedTitleId,
    bool appRunning,
    switchu::smi::LockReturnTarget returnTarget) {
    m_launcher.setStartupStatus(suspendedTitleId, appRunning);

    m_lockReturnTarget =
        returnTarget == switchu::smi::LockReturnTarget::Game
            ? switchu::smi::LockReturnTarget::Game
            : switchu::smi::LockReturnTarget::Home;

    // Securite : GAME n'est jamais une destination valide sans application.
    if (m_lockReturnTarget == switchu::smi::LockReturnTarget::Game &&
        suspendedTitleId == 0) {
        m_lockReturnTarget = switchu::smi::LockReturnTarget::Home;
    }

    m_skipStartupLock = false;

    DebugLog::log("[lockscreen-route] startup target=%s suspended=0x%016lX",
                  m_lockReturnTarget == switchu::smi::LockReturnTarget::Game
                      ? "GAME"
                      : "HOME",
                  suspendedTitleId);
}
#endif

void WiiUMenuApp::handleLockScreen(float dt) {
    // V9 memory ownership: while the lockscreen is visible the HOME preview
    // engine must own zero video/static preview textures.
    if (m_background)
        m_background->setPreviewActive(false);

    m_lockScreenPulse += dt;
    m_lockScreenReveal = std::min(1.f, m_lockScreenReveal + dt / 0.48f);
    m_lockPressFlash = std::max(0.f, m_lockPressFlash - dt * 2.8f);

    m_lockBatteryPollTimer -= dt;
    if (m_lockBatteryPollTimer <= 0.f) {
        m_lockBatteryPollTimer = 2.f;

        u32 percentage = m_lockBatteryPercent;
        if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percentage)))
            m_lockBatteryPercent = std::min<uint32_t>(percentage, 100u);

        PsmChargerType charger = PsmChargerType_Unconnected;
        if (R_SUCCEEDED(psmGetChargerType(&charger)))
            m_lockBatteryCharging = charger != PsmChargerType_Unconnected;

        if (m_lockScreenView)
            m_lockScreenView->setBatteryStatus(m_lockBatteryPercent,
                                               m_lockBatteryCharging);
    }

    auto syncView = [this, dt]() {
        if (!m_lockScreenView)
            return;

#ifdef SWITCHU_MENU
        const bool returnToGame =
            m_lockReturnTarget == switchu::smi::LockReturnTarget::Game &&
            m_launcher.suspendedTitleId() != 0;
#else
        const bool returnToGame = false;
#endif
        (void)returnToGame;

        // LockScreenView no longer exposes a return-target setter. The actual
        // GAME/HOME handoff is owned by this routing layer after the 1.20 s
        // unlock transition, so the visual view only needs progress/transition.
        m_lockScreenView->setProgress(m_lockPressCount, m_lockPressFlash);
        m_lockScreenView->setTransition(m_lockScreenOpacity,
                                        m_lockScreenReveal,
                                        m_lockUnlockProgress,
                                        m_lockScreenPulse,
                                        m_lockScreenUnlocking);
        m_lockScreenView->update(dt);
    };

    if (m_lockScreenUnlocking) {
        // Le timer final est l'unique autorite de la transition.
        // Rien ne peut lancer le jeu avant la fin de ces 1,20 seconde.
        constexpr float kFinalUnlockDuration = 1.20f;
        m_lockUnlockProgress = std::min(
            1.f,
            m_lockUnlockProgress + dt / kFinalUnlockDuration
        );

#ifdef SWITCHU_MENU
        const bool returnToGame =
            m_lockReturnTarget == switchu::smi::LockReturnTarget::Game &&
            m_launcher.suspendedTitleId() != 0;
#else
        const bool returnToGame = false;
#endif

        if (returnToGame) {
            // Garder le lockscreen opaque jusqu'a la commande de reprise.
            m_lockScreenOpacity = 1.f;
        } else {
            // HOME : animation pleine pendant 75 %, puis court fondu final.
            const float fadeT = std::clamp(
                (m_lockUnlockProgress - 0.75f) / 0.25f,
                0.f,
                1.f
            );
            m_lockScreenOpacity = 1.f - v74SmoothStep(fadeT);
        }

        syncView();

        if (m_lockUnlockProgress >= 1.f) {
#ifdef SWITCHU_MENU
            if (returnToGame) {
                DebugLog::log("[lockscreen-route] 1.20s complete -> GAME");
                const Result resumeRc = m_launcher.resumeApplication();
                if (R_SUCCEEDED(resumeRc)) {
                    // AppletLauncher attend le GPU puis demande la sortie de
                    // Switch U. Le lockscreen reste opaque jusqu'a ce handoff.
                    return;
                }

                DebugLog::log(
                    "[lockscreen-route] resume failed rc=0x%X -> HOME fallback",
                    resumeRc
                );
                m_lockReturnTarget = switchu::smi::LockReturnTarget::Home;
            } else {
                DebugLog::log("[lockscreen-route] 1.20s complete -> HOME");
            }
#else
            DebugLog::log("[lockscreen-route] 1.20s complete -> HOME (homebrew)");
#endif

            if (m_lockScreenView) {
                m_lockScreenView->clearSuspendedGame();
                m_lockScreenView->setVisible(false);
            }

            m_iconStreamer.clearPinnedIndex();
            m_lockPinnedIconIndex = -1;

            if (m_grid) {
                m_iconStreamer.onPageChanged(m_grid->currentPage(),
                                             std::max(1, m_grid->iconsPerPage()),
                                             app().gpu(),
                                             app().renderer(),
                                             m_grid->allIcons());
            }

            m_lockScreenActive = false;
            if (m_background)
                m_background->setPreviewActive(true);
            m_lockScreenUnlocking = false;
            m_lockPressCount = 0;
            m_lockPressResetTimer = 0.f;
            m_lockScreenOpacity = 0.f;
            m_lockScreenReveal = 0.f;
            m_lockUnlockProgress = 0.f;
            m_lockPressFlash = 0.f;
            m_audio.playSfx(Sfx::ModalHide);
        }
        return;
    }

    if (m_lockPressResetTimer > 0.f) {
        m_lockPressResetTimer = std::max(0.f, m_lockPressResetTimer - dt);
        if (m_lockPressResetTimer <= 0.f && m_lockPressCount > 0) {
            m_lockPressCount = 0;
            m_lockPressFlash = 0.72f;
            m_audio.playSfx(Sfx::ToggleOff);
        }
    }

    if (!app().input().isDown(nxui::Button::A)) {
        syncView();
        return;
    }

    ++m_lockPressCount;
    m_lockPressResetTimer = 1.55f;
    m_lockPressFlash = 1.f;

    if (m_lockPressCount < 3) {
        m_audio.playSfx(Sfx::Navigate);
        syncView();
        return;
    }

    m_lockPressCount = 3;
    m_lockPressResetTimer = 0.f;
    m_lockScreenUnlocking = true;
    m_lockUnlockProgress = 0.f;
    m_lockScreenOpacity = 1.f;
    m_audio.playSfx(Sfx::ConfirmPositive);

#ifdef SWITCHU_MENU
    const bool finalReturnToGame =
        m_lockReturnTarget == switchu::smi::LockReturnTarget::Game &&
        m_launcher.suspendedTitleId() != 0;
#else
    const bool finalReturnToGame = false;
#endif
    DebugLog::log(
        "[lockscreen-route] third A -> final 1.20s target=%s",
        finalReturnToGame ? "GAME" : "HOME"
    );
    syncView();
}

#ifdef SWITCHU_MENU
void WiiUMenuApp::handleSystemAction(SysAction a) {
    auto leaveLockscreenForHome = [this](const char* reason) {
        DebugLog::log("[lockscreen-route] %s -> HOME (no lockscreen)", reason);

        m_lockReturnTarget = switchu::smi::LockReturnTarget::Home;
        m_launcher.setAppHasForeground(false);

        if (m_lockScreenView) {
            m_lockScreenView->clearSuspendedGame();
            m_lockScreenView->setVisible(false);
        }

        m_lockScreenActive = false;
        if (m_background)
            m_background->setPreviewActive(true);
        m_lockScreenUnlocking = false;
        m_lockScreenOpacity = 0.f;
        m_lockScreenReveal = 0.f;
        m_lockUnlockProgress = 0.f;
        m_lockPressCount = 0;
        m_lockPressResetTimer = 0.f;
        m_lockPressFlash = 0.f;
        m_iconStreamer.clearPinnedIndex();
        m_lockPinnedIconIndex = -1;

        closeActiveOverlays();
        markSuspendedIcon(m_launcher.suspendedTitleId());

        if (m_grid) {
            if (m_launcher.suspendedTitleId() != 0) {
                focusTitle(m_launcher.suspendedTitleId());
            } else if (auto* target = m_grid->focusManager().current()) {
                focusManager().setFocus(target);
                updateCursor();
            }
        }
    };

    switch (a) {
        case SysAction::HomeButton:
            // Le code historique transforme aussi ApplicationSuspended et
            // ApplicationExited en HomeButton. Ce n'est plus un probleme :
            // les trois cas doivent tous finir sur HOME, sans lockscreen.
            leaveLockscreenForHome("HomeButton");
            break;

        case SysAction::ApplicationSuspended:
            m_launcher.setAppHasForeground(false);
            leaveLockscreenForHome("ApplicationSuspended");
            break;

        case SysAction::ApplicationExited:
            m_launcher.setAppRunning(false);
            m_launcher.setAppHasForeground(false);
            m_launcher.setSuspendedTitleId(0);
            leaveLockscreenForHome("ApplicationExited");
            break;

        case SysAction::WakeUp: {
            // V7.4.3 direct : le vieux pump ne transmet pas notif.payload.
            // On relit donc le SystemStatus du daemon au reveil. Le daemon
            // contient la destination capturee AVANT la mise en veille.
            switchu::smi::SystemStatus status{};
            const Result statusRc = switchu::menu::smi_cmd::getSystemStatus(status);
            if (R_SUCCEEDED(statusRc)) {
                m_launcher.setAppRunning(status.app_running);
                m_launcher.setAppHasForeground(false);
                m_launcher.setSuspendedTitleId(status.suspended_app_id);
                m_lockReturnTarget =
                    status.lock_return_target == switchu::smi::LockReturnTarget::Game
                        ? switchu::smi::LockReturnTarget::Game
                        : switchu::smi::LockReturnTarget::Home;

                DebugLog::log(
                    "[lockscreen-route] WakeUp status target=%s suspended=0x%016lX running=%d",
                    m_lockReturnTarget == switchu::smi::LockReturnTarget::Game
                        ? "GAME"
                        : "HOME",
                    status.suspended_app_id,
                    status.app_running ? 1 : 0
                );
            } else {
                DebugLog::log(
                    "[lockscreen-route] WakeUp status read failed rc=0x%X; keeping target=%s",
                    statusRc,
                    m_lockReturnTarget == switchu::smi::LockReturnTarget::Game
                        ? "GAME"
                        : "HOME"
                );
            }

            if (m_lockReturnTarget == switchu::smi::LockReturnTarget::Game &&
                m_launcher.suspendedTitleId() == 0) {
                m_lockReturnTarget = switchu::smi::LockReturnTarget::Home;
            }

            markSuspendedIcon(m_launcher.suspendedTitleId());
            closeActiveOverlays();
            if (m_launcher.suspendedTitleId() != 0)
                focusTitle(m_launcher.suspendedTitleId());

            DebugLog::log(
                "[lockscreen-route] WakeUp -> lockscreen target=%s suspended=0x%016lX",
                m_lockReturnTarget == switchu::smi::LockReturnTarget::Game
                    ? "GAME"
                    : "HOME",
                m_launcher.suspendedTitleId()
            );
            if (m_background)
                m_background->setPreviewActive(false);
            showLockScreen();
            break;
        }

        default:
            break;
    }
}
#endif
