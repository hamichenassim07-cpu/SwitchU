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
        m_musicScreen->onSessionGuard([this](bool active) {
            // V0.02: HOME BGM yields only when a real SwitchU Music session
            // exists. Keep the transition soft instead of hard-stopping the
            // HOME soundtrack as soon as the Music category opens.
            if (active) {
                if (m_audio.isPlaying())
                    m_audio.fadeOutForGame(350);
            } else {
                m_audio.playHome(350);
            }
        });
        DebugLog::log("[music] native Music V0.02 UI created");
    }

    if (m_overlayLayer && m_musicScreen->parent() != m_overlayLayer.get()) {
        if (auto* oldParent = m_musicScreen->parent())
            oldParent->removeChild(m_musicScreen.get());
        m_overlayLayer->addChild(m_musicScreen);

        // Keep system dialogs/settings above Music. V0.01 attached Music last,
        // which meant its global mini-player could later paint on top of
        // unrelated full-screen overlays. Re-adding known overlays restores a
        // stable z-order while leaving the HOME cursor behind Music.
        auto raise = [this](const std::shared_ptr<nxui::Widget>& widget) {
            if (!widget || widget->parent() != m_overlayLayer.get()) return;
            m_overlayLayer->removeChild(widget.get());
            m_overlayLayer->addChild(widget);
        };
        raise(std::static_pointer_cast<nxui::Widget>(m_settings));
        raise(std::static_pointer_cast<nxui::Widget>(m_themeShop));
        raise(std::static_pointer_cast<nxui::Widget>(m_userSelect));
        raise(std::static_pointer_cast<nxui::Widget>(m_dialog));
        raise(std::static_pointer_cast<nxui::Widget>(m_progressDialog));
        raise(std::static_pointer_cast<nxui::Widget>(m_launchAnim));
        raise(std::static_pointer_cast<nxui::Widget>(m_pointerCursor));
        DebugLog::log("[music] MusicScreen attached to overlay render tree (stable z-order)");
    }
}

void WiiUMenuApp::showMusic() {
    createMusic();
    if (!m_musicScreen) return;
    if (!m_userAvatarButtons.empty() && m_userAvatarButtons.front())
        m_musicScreen->setProfileTexture(m_userAvatarButtons.front()->avatarTexture());

    // V0.02: merely entering Music does not silence HOME BGM. The session
    // guard fades it only after a user track actually becomes active.
    m_musicReturnHomeCategory = m_clock ? m_clock->homeCategory() : 1;
    if (m_clock)
        m_clock->setHomeCategory(2);

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
    DebugLog::log("[music-diag] CLOSE_SAFE begin session=%d returnCategory=%d",
                  sessionActive ? 1 : 0, m_musicReturnHomeCategory);

    // First remove Music from input/render ownership. Its audio service remains
    // independent and may continue playing.
    m_musicScreen->hide();
    DebugLog::log("[music-diag] CLOSE_SAFE music_hidden");

    // Restore one stable HOME focus target, but do not update the cursor yet:
    // setHomeApplicationsCategory() may rebuild the visible carousel/filter.
    if (m_grid) {
        if (auto* cur = m_grid->focusManager().current())
            focusManager().setFocus(cur);
    }
    DebugLog::log("[music-diag] CLOSE_SAFE focus_restored");

    // Restore the HOME category while Music is already render-silent. Music
    // audio itself stays in the daemon and therefore survives this transition.
    const bool applications = m_musicReturnHomeCategory != 0;
    DebugLog::log("[music-diag] CLOSE_SAFE category_begin applications=%d", applications ? 1 : 0);
    setHomeApplicationsCategory(applications);
    if (m_clock)
        m_clock->setHomeCategory(applications ? 1 : 0);
    DebugLog::log("[music-diag] CLOSE_SAFE category_done");

    // Cursor/halo are updated only after HOME is back in its final category.
    updateCursor();
    DebugLog::log("[music-diag] CLOSE_SAFE cursor_done");

    // If no local session exists, ensure HOME theme audio comes back softly.
    if (!sessionActive)
        m_audio.playHome(350);

    DebugLog::log("[music-diag] CLOSE_SAFE complete session=%d returnCategory=%d",
                  sessionActive ? 1 : 0, applications ? 1 : 0);
}
