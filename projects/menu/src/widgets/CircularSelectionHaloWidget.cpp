#include "CircularSelectionHaloWidget.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>

void CircularSelectionHaloWidget::setTargetRect(const nxui::Rect& target) {
    // Force a true circle even if a focus rect is a few pixels wider/taller.
    const float side = std::max(target.width, target.height) + 8.f;
    const float cx = target.x + target.width * 0.5f;
    const float cy = target.y + target.height * 0.5f;
    setRect({cx - side * 0.5f, cy - side * 0.5f, side, side});
}

void CircularSelectionHaloWidget::onRender(nxui::Renderer& ren) {
    if (m_opacity <= 0.001f || m_rect.width <= 1.f || m_rect.height <= 1.f)
        return;

    const float radius = std::min(m_rect.width, m_rect.height) * 0.5f;

    // V10.9: clean white circular selection, deliberately quieter than the
    // historical blue/purple cursor. No position interpolation is used.
    ren.drawRoundedRect(
        m_rect.expanded(2.0f),
        nxui::Color(1.f, 1.f, 1.f, 0.09f * m_opacity),
        radius + 2.0f
    );
    ren.drawRoundedRectOutline(
        m_rect.expanded(4.f),
        nxui::Color(1.f, 1.f, 1.f, 0.18f * m_opacity),
        radius + 4.f,
        7.f
    );
    ren.drawRoundedRectOutline(
        m_rect.expanded(1.5f),
        nxui::Color(1.f, 1.f, 1.f, 0.30f * m_opacity),
        radius + 1.5f,
        4.f
    );
    ren.drawRoundedRectOutline(
        m_rect,
        nxui::Color(1.f, 1.f, 1.f, 0.92f * m_opacity),
        radius,
        2.6f
    );
}
