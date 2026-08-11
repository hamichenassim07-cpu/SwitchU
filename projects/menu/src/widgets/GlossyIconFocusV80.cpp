// Switch U HOME V8.1 - hook de focus direct, sans script.
#define SWITCHU_V80_FOCUS_STRONG 1
#include "GlossyIcon.hpp"
#include "WaraWaraBackground.hpp"

void GlossyIcon::onFocusGained() {
    // Comportement historique conserve a l'identique.
    m_focused = true;
    m_focusScale.set(1.045f, 0.18f, nxui::Easing::outBack);
    m_focusGlow.set(0.f, 0.01f, nxui::Easing::outCubic);

    // V8.1 : une simple selection suffit. Aucun clic A n'est necessaire.
    if (m_titleId != 0)
        WaraWaraBackground::notifySelectedGame(m_titleId);
}
