#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>

namespace {
constexpr int kVisibleIcons = 4;
constexpr int kTrailingEmptySlots = 4;
constexpr float kLargeIconSize = 255.f;
constexpr float kLargeIconGap = 34.f;
}

IconGrid::IconGrid() {
    // Reposition the row immediately when the selected game changes.
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

void IconGrid::updateDisplayCount() {
    int lastRealIcon = -1;

    for (int i = 0; i < static_cast<int>(m_allIcons.size()); ++i) {
        const auto& icon = m_allIcons[i];
        if (icon && icon->titleId() != 0)
            lastRealIcon = i;
    }

    // Keep a few visible empty slots after the last real application, instead
    // of displaying the many pages of empty slots created by the stock grid.
    const int wanted = std::max(
        m_visibleCols,
        lastRealIcon >= 0 ? lastRealIcon + 1 + kTrailingEmptySlots
                          : m_visibleCols
    );

    m_displayCount = std::clamp(
        wanted,
        1,
        std::max(1, static_cast<int>(m_allIcons.size()))
    );
}

void IconGrid::reconfigureLayout(int cols, int rows,
                                 float cellW, float cellH,
                                 float padX, float padY)
{
    (void)cols;
    (void)rows;
    (void)cellW;
    (void)cellH;
    (void)padX;
    (void)padY;

    m_visibleCols = kVisibleIcons;
    m_cellW = kLargeIconSize;
    m_cellH = kLargeIconSize;
    m_padX = kLargeIconGap;

    updateDisplayCount();

    const float visibleW =
        m_visibleCols * m_cellW + (m_visibleCols - 1) * m_padX;

    m_originX = m_rect.x + (m_rect.width - visibleW) * 0.5f;
    m_originY = m_rect.y + (m_rect.height - m_cellH) * 0.5f;

    rebuildFocusRow();
}

void IconGrid::setPage(int page) {
    (void)page;
    layoutCarousel();
}

void IconGrid::rebuildFocusRow() {
    nxui::Widget* previousFocus = m_focus.current();

    clearChildren();

    std::vector<nxui::Widget*> focusItems;
    focusItems.reserve(m_displayCount);

    for (int i = 0; i < m_displayCount; ++i) {
        auto& icon = m_allIcons[i];
        if (!icon)
            continue;

        // Empty slots and icons that were initially off-screen must still be
        // visible when the carousel reaches them.
        icon->forceVisible();
        addChild(icon);

        if (icon->isFocusable())
            focusItems.push_back(icon.get());
    }

    // One continuous row: left/right always moves by one application.
    m_focus.setGrid(focusItems, std::max(1, static_cast<int>(focusItems.size())));

    if (previousFocus) {
        auto it = std::find(focusItems.begin(), focusItems.end(), previousFocus);
        if (it != focusItems.end())
            m_focus.setFocus(previousFocus);
    }

    layoutCarousel();
}

void IconGrid::layoutCarousel() {
    if (m_displayCount <= 0)
        return;

    int focused = focusedGlobalIndex();
    if (focused < 0 || focused >= m_displayCount)
        focused = 0;

    const int maxStart = std::max(0, m_displayCount - m_visibleCols);

    // Keep the selected icon near the middle, except at the beginning/end.
    m_windowStart = std::clamp(
        focused - (m_visibleCols / 2),
        0,
        maxStart
    );

    const float step = m_cellW + m_padX;

    for (int i = 0; i < m_displayCount; ++i) {
        auto& icon = m_allIcons[i];
        if (!icon)
            continue;

        const float x = m_originX + (i - m_windowStart) * step;
        icon->setRect({x, m_originY, m_cellW, m_cellH});

        const bool visible =
            i >= m_windowStart &&
            i < m_windowStart + m_visibleCols;

        if (visible)
            icon->forceVisible();
    }
}

int IconGrid::focusedGlobalIndex() const {
    auto* current = m_focus.current();
    if (!current)
        return -1;

    for (int i = 0; i < m_displayCount; ++i) {
        if (m_allIcons[i].get() == current)
            return i;
    }

    return -1;
}

bool IconGrid::focusGlobalIndex(int idx) {
    if (idx < 0 || idx >= m_displayCount)
        return false;

    if (!m_allIcons[idx] || !m_allIcons[idx]->isFocusable())
        return false;

    m_focus.setFocus(m_allIcons[idx].get());
    layoutCarousel();
    return true;
}

bool IconGrid::swapSlots(int a, int b) {
    if (a < 0 || b < 0 ||
        a >= m_displayCount || b >= m_displayCount)
        return false;

    if (a == b)
        return true;

    nxui::Widget* focused = m_focus.current();
    std::swap(m_allIcons[a], m_allIcons[b]);

    rebuildFocusRow();

    if (focused)
        m_focus.setFocus(focused);

    layoutCarousel();
    return true;
}

std::vector<GlossyIcon*> IconGrid::pageIcons() const {
    std::vector<GlossyIcon*> out;
    out.reserve(m_displayCount);

    for (int i = 0; i < m_displayCount; ++i) {
        if (m_allIcons[i])
            out.push_back(m_allIcons[i].get());
    }

    return out;
}

int IconGrid::hitTest(float screenX, float screenY) const {
    const int end = std::min(m_windowStart + m_visibleCols, m_displayCount);

    for (int i = m_windowStart; i < end; ++i) {
        if (!m_allIcons[i])
            continue;

        if (m_allIcons[i]->focusRect().contains(screenX, screenY))
            return i;
    }

    return -1;
}

void IconGrid::startAppearAnimation() {
    const int end = std::min(m_windowStart + m_visibleCols, m_displayCount);

    int order = 0;
    for (int i = m_windowStart; i < end; ++i) {
        if (!m_allIcons[i])
            continue;

        m_allIcons[i]->forceVisible();
        m_allIcons[i]->startAppear(order * 0.06f);
        ++order;
    }
}

void IconGrid::startWaveTransition(int targetPage) {
    (void)targetPage;

    // The horizontal carousel has no pages.
    if (m_onPageSwitched)
        m_onPageSwitched();
}

void IconGrid::onUpdate(float dt) {
    (void)dt;
}

void IconGrid::render(nxui::Renderer& renderer) {
    if (!m_visible || m_opacity <= 0.f)
        return;

    // Hide off-screen icons so they do not cover the HUD or side buttons.
    renderer.pushClipRect(m_rect);
    for (auto& child : m_children)
        child->render(renderer);
    renderer.popClipRect();
}

void IconGrid::onRender(nxui::Renderer&) {
}
