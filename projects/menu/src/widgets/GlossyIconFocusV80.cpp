// Switch U HOME V8.0A.2 - hook de focus direct, sans script.
#define SWITCHU_V80_FOCUS_STRONG 1
#include "GlossyIcon.hpp"
#include "WaraWaraBackground.hpp"

void GlossyIcon::onFocusGained() {
    // Comportement historique conserve a l'identique.
    m_focused = true;
    m_focusScale.set(1.10f, 0.20f, nxui::Easing::outBack);
    m_focusGlow.set(1.f, 0.16f, nxui::Easing::outCubic);

    // V8.0A.2 : une simple selection suffit. Aucun clic A n'est necessaire.
    if (m_titleId != 0)
        WaraWaraBackground::notifySelectedGame(m_titleId);
}
