#pragma once
#include <nxui/core/Renderer.hpp>
#include <algorithm>

namespace switchu::homeui {
// Local, inexpensive focus mark. No moving cursor between the upper profile
// and the lower system commands; outgoing and incoming marks cross-fade.
inline void drawControlFocus(nxui::Renderer& ren, const nxui::Rect& rect,
                             float amount, float alpha) {
    if (amount <= .001f) return;
    const nxui::Color accent(.40f, .69f, 1.f, alpha * amount);
    const float radius = std::min(rect.width, rect.height) * .5f;
    ren.drawRoundedRect(rect.expanded(4.f), accent.withAlpha(.10f * alpha * amount), radius + 4.f);
    const float width = rect.width * (.30f + .16f * amount);
    ren.drawRoundedRect({rect.x + (rect.width - width) * .5f,
                         rect.y + rect.height + 7.f, width, 2.5f}, accent, 1.25f);
}
}
