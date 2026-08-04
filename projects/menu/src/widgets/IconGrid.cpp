#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

IconGrid::IconGrid() {
    // The application owns the global focus manager. Each time it mirrors the
    // selected game into this local manager, recenter the horizontal row.
    m_focus.onFocusChanged([this](nxui::Widget*, nxui::Widget*) {
        layoutCarousel();
    });
}

void IconGrid::setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
                     int cols, int rows,
                     float cellW, float cellH,
                     float padX, float padY)
{
    m_allIcons = std::move(icons);
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

void IconGrid::reconfigureLayout(int cols, int rows,
                                 float cellW, float cellH,
                                 float padX, float padY)
{
    (void)rows;
    (void)padY;

    // Preserve the user's column preference as the number of visible items,
    // but turn the whole model into one continuous horizontal row.
    m_visibleCols = std::clamp(cols, 3, 8);
    m_cols = std::max(1, static_cast<int>(m_allIcons.size()));
    m_rows = 1;

    m_cellW = cellW;
    m_cellH = cellH;
    m_padX  = padX;
    m_padY  = 0.f;

    // One continuous row: no grid pages.
    m_page = 0;
    m_totalPages = 1;

    const float visibleW =
        m_visibleCols * m_cellW + (m_visibleCols - 1) * m_padX;

    m_originX = (m_rect.width - visibleW) * 0.5f + m_rect.x;
    m_originY = (m_rect.height - m_cellH) * 0.5f + m_rect.y;

    layoutPage();
}

void IconGrid::setPage(int page) {
    (void)page;
    m_page = 0;
    layoutCarousel();
}

void IconGrid::layoutPage() {
    nxui::Widget* previousFocus = m_focus.current();

    clearChildren();

    std::vector<nxui::Widget*> focusItems;
    focusItems.reserve(m_allIcons.size());

    for (auto& icon : m_allIcons) {
        if (!icon)
            continue;

        addChild(icon);
        if (icon->isFocusable())
            focusItems.push_back(icon.get());
    }

    // All items are one row. This keeps left/right movement continuous.
    m_focus.setGrid(focusItems, std::max(1, static_cast<int>(focusItems.size())));

    if (previousFocus)
        m_focus.setFocus(previousFocus);

    layoutCarousel();
}

void IconGrid::layoutCarousel() {
    if (m_allIcons.empty())
        return;

    int focused = focusedGlobalIndex();
    if (focused < 0)
        focused = 0;

    const int visibleCount = std::max(1, m_visibleCols);
    const int maxStart =
        std::max(0, static_cast<int>(m_allIcons.size()) - visibleCount);

    // Keep the selected game near the middle, except at both ends.
    const int desiredStart = focused - visibleCount / 2;
    const int windowStart = std::clamp(desiredStart, 0, maxStart);

    const float step = m_cellW + m_padX;

    for (int i = 0; i < static_cast<int>(m_allIcons.size()); ++i) {
        auto& icon = m_allIcons[i];
        if (!icon)
            continue;

        const float x = m_originX + (i - windowStart) * step;
        icon->setRect({x, m_originY, m_cellW, m_cellH});
    }
}

int IconGrid::focusedGlobalIndex() const {
    auto* current = m_focus.current();
    if (!current)
        return -1;

    for (int i = 0; i < static_cast<int>(m_allIcons.size()); ++i) {
        if (m_allIcons[i].get() == current)
            return i;
    }
    return -1;
}

bool IconGrid::focusGlobalIndex(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_allIcons.size()))
        return false;
    if (!m_allIcons[idx] || !m_allIcons[idx]->isFocusable())
        return false;

    m_focus.setFocus(m_allIcons[idx].get());
    layoutCarousel();
    return true;
}

bool IconGrid::swapSlots(int a, int b) {
    if (a < 0 || b < 0 ||
        a >= static_cast<int>(m_allIcons.size()) ||
        b >= static_cast<int>(m_allIcons.size()))
        return false;

    if (a == b)
        return true;

    nxui::Widget* focused = m_focus.current();
    std::swap(m_allIcons[a], m_allIcons[b]);

    layoutPage();

    if (focused)
        m_focus.setFocus(focused);

    layoutCarousel();
    return true;
}

std::vector<GlossyIcon*> IconGrid::pageIcons() const {
    std::vector<GlossyIcon*> out;
    out.reserve(m_allIcons.size());

    for (const auto& icon : m_allIcons) {
        if (icon)
            out.push_back(icon.get());
    }
    return out;
}

int IconGrid::hitTest(float screenX, float screenY) const {
    for (int i = 0; i < static_cast<int>(m_allIcons.size()); ++i) {
        if (!m_allIcons[i])
            continue;

        const nxui::Rect rect = m_allIcons[i]->focusRect();
        if (rect.contains(screenX, screenY))
            return i;
    }
    return -1;
}

void IconGrid::startAppearAnimation() {
    const float left = m_rect.x;
    const float right = m_rect.x + m_rect.width;

    int visibleOrder = 0;
    for (auto& icon : m_allIcons) {
        if (!icon)
            continue;

        const nxui::Rect rect = icon->rect();
        if (rect.x + rect.width < left || rect.x > right)
            continue;

        const float delay = visibleOrder * 0.055f;
        icon->startAppear(delay);
        ++visibleOrder;
    }
}

void IconGrid::startWaveTransition(int targetPage) {
    (void)targetPage;

    // The carousel has no pages. Keep this method for compatibility with the
    // existing ZL/ZR and touch code.
    m_page = 0;
    m_totalPages = 1;
    m_waveActive = false;
    m_wavePhase = WavePhase::Idle;

    if (m_onPageSwitched)
        m_onPageSwitched();
}

void IconGrid::onUpdate(float dt) {
    if (m_waveActive && m_wavePhase == WavePhase::Animating) {
        m_waveTime += dt;
        if (m_waveTime >= m_waveDuration) {
            m_waveActive = false;
            m_wavePhase = WavePhase::Idle;
        }
    }
}

void IconGrid::render(nxui::Renderer& renderer) {
    if (!m_visible || m_opacity <= 0.f)
        return;

    // Prevent off-screen games from drawing over the sidebars and HUD.
    renderer.pushClipRect(m_rect);
    for (auto& child : m_children)
        child->render(renderer);
    renderer.popClipRect();
}

void IconGrid::onRender(nxui::Renderer&) {
}
