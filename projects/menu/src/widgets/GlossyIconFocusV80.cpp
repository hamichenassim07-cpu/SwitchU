// Switch U HOME V8.1 - hook de focus direct, sans script.
#define SWITCHU_V80_FOCUS_STRONG 1
#include "GlossyIcon.hpp"
#include "WaraWaraBackground.hpp"

void GlossyIcon::onFocusGained() {
    // Comportement historique conserve a l'identique.
    m_focused = true;
    m_focusScale.set(1.085f, 0.22f, nxui::Easing::outBack);
    m_focusGlow.set(0.f, 0.08f, nxui::Easing::outCubic);

    // V8.1 : une simple selection suffit. Aucun clic A n'est necessaire.
    if (m_titleId != 0)
        WaraWaraBackground::notifySelectedGame(m_titleId);
}
