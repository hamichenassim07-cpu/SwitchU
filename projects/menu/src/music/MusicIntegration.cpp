#include "core/WiiUMenuApp.hpp"
#include "core/DebugLog.hpp"

void WiiUMenuApp::createMusic() {
    // wireGlobalActions() is called before the HOME overlay layer is built.
    // Music can therefore already exist here while still having no parent.
    // Always (re)attach it once m_overlayLayer becomes available; otherwise
    // it receives focus/input but never participates in the render tree.
    if (!m_musicScreen) {
        m_musicScreen = std::make_shared<switchu::menu::music::MusicScreen>();
        m_musicScreen->setFonts(&m_fontNormal, &m_fontSmall, &m_fontIcons);
        m_musicScreen->onClose([this]() { closeMusic(); });
        m_musicScreen->onSessionGuard([this]() {
            if (m_audio.isPlaying())
                m_audio.stop();
        });
        DebugLog::log("[music] full-screen Music V0.01 UI created");
    }

    if (m_overlayLayer && m_musicScreen->parent() != m_overlayLayer.get()) {
        if (auto* oldParent = m_musicScreen->parent())
            oldParent->removeChild(m_musicScreen.get());
        m_overlayLayer->addChild(m_musicScreen);
        DebugLog::log("[music] MusicScreen attached to overlay render tree");
    }
}

void WiiUMenuApp::showMusic() {
    createMusic();
    if (!m_musicScreen) return;

    // A user-music session must never be mixed with the HOME theme playlist.
    // Playback itself is owned by the daemon and survives this UI transition.
    m_audio.stop();

    if (m_userSelect && m_userSelect->isActive()) m_userSelect->hide();
    if (m_dialog && m_dialog->isActive()) m_dialog->hide();
    if (m_settings && m_settings->isActive()) m_settings->hide();
    if (m_themeShop && m_themeShop->isActive()) m_themeShop->hide();

    m_musicScreen->show();
    if (m_cursor) m_cursor->setVisible(false);
    if (m_systemSelectionHalo) m_systemSelectionHalo->setVisible(false);
    focusManager().setFocus(m_musicScreen.get());
    DebugLog::log("[music] opened full-screen Music application");
}

void WiiUMenuApp::closeMusic() {
    if (!m_musicScreen || !m_musicScreen->isActive()) return;

    const bool sessionActive = m_musicScreen->hasMusicSession();
    m_musicScreen->hide();

    if (m_grid) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
        updateCursor();
    }

    // If local music is active, the daemon remains the only music source.
    // When there is no local session, restore the user's normal HOME theme.
    if (!sessionActive)
        m_audio.playHome(220);

    DebugLog::log("[music] closed UI sessionActive=%d", sessionActive ? 1 : 0);
}
