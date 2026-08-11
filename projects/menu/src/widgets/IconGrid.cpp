#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {
// V10: one hero cover + one single neighbour size. There is deliberately
// no third "outer" size anymore: every non-selected visible cover is 230 px.
constexpr int kVisibleIcons = 5;
constexpr float kSelectedIconSize = 310.f;
constexpr float kNeighborIconSize = 230.f;
constexpr float kCarouselGap = 12.f;
constexpr float kCarouselBaselineY = 512.f;

float iconSizeForDistance(float distance) {
    distance = std::abs(distance);
    if (distance <= 1.f) {
        return kSelectedIconSize +
               (kNeighborIconSize - kSelectedIconSize) * distance;
    }
    return kNeighborIconSize;
}

// Keep a real 12 px visual gap even while the selection animates between two
// covers. A fixed centre-to-centre step cannot do that with 310/230 px cards.
float carouselCenterOffset(float distance) {
    const float sign = distance < 0.f ? -1.f : 1.f;
    const float a = std::abs(distance);
    const float firstStep =
        kSelectedIconSize * 0.5f +
        kNeighborIconSize * 0.5f +
        kCarouselGap;
    const float neighborStep =
        kNeighborIconSize + kCarouselGap;

    const float magnitude =
        (a <= 1.f)
            ? a * firstStep
            : firstStep + (a - 1.f) * neighborStep;
    return sign * magnitude;
}

// Plus le geste est rapide, plus la vitesse initiale est forte.
// La friction reste douce pour permettre aux gestes puissants
// d'aller sensiblement plus loin.
constexpr float kInertiaFriction = 4.4f;
constexpr float kMinimumInertiaSpeed = 0.14f;
constexpr float kMaximumInertiaSpeed = 14.f;
constexpr float kSnapSpeed = 14.f;
}

IconGrid::IconGrid() {
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
    // V10.6: carousel cards use the legacy frosted path, never the new C2
    // Liquid Glass shader. This prevents circular lens clipping inside square
    // application cards while keeping the pre-C2 glass look.
    for (auto& icon : m_allIcons) {
        if (icon)
            icon->setLiquidGlassShaderEnabled(false);
    }
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

void IconGrid::setApplicationTitleIds(const std::vector<uint64_t>& titleIds) {
    m_applicationTitleIds.clear();
    for (uint64_t titleId : titleIds) {
        if (titleId != 0)
            m_applicationTitleIds.insert(titleId);
    }

    updateDisplayCount();
    rebuildFocusRow();
}

void IconGrid::setCarouselFocusActive(bool active) {
    if (m_carouselFocusActive == active)
        return;
    m_carouselFocusActive = active;
    layoutAtScrollPosition();
}

void IconGrid::setShowApplications(bool showApplications) {
    if (m_showApplications == showApplications)
        return;

    m_showApplications = showApplications;
    m_scrollPosition = 0.f;
    m_scrollVelocity = 0.f;
    m_touchScrolling = false;
    m_inertiaActive = false;
    m_snapActive = false;
    m_pendingSettledFocusIndex = -1;

    updateDisplayCount();
    rebuildFocusRow();
}

int IconGrid::firstVisibleGlobalIndex() const {
    return m_displayIndices.empty() ? -1 : m_displayIndices.front();
}

int IconGrid::displayPositionForGlobalIndex(int globalIndex) const {
    for (int i = 0; i < static_cast<int>(m_displayIndices.size()); ++i) {
        if (m_displayIndices[i] == globalIndex)
            return i;
    }
    return -1;
}

int IconGrid::globalIndexForDisplayPosition(int displayPosition) const {
    if (displayPosition < 0 ||
        displayPosition >= static_cast<int>(m_displayIndices.size()))
        return -1;
    return m_displayIndices[displayPosition];
}

bool IconGrid::isGlobalIndexVisible(int globalIndex) const {
    return displayPositionForGlobalIndex(globalIndex) >= 0;
}

int IconGrid::visibleSlotCount() const {
    return std::max(
        1,
        std::min(m_visibleCols, m_displayCount)
    );
}

float IconGrid::maxScrollPosition() const {
    // V10: m_scrollPosition represents the item located under the exact
    // horizontal centre of the HOME, not the first item of a visible window.
    // The first and last games are therefore allowed to sit at screen centre.
    return static_cast<float>(
        std::max(0, m_displayCount - 1)
    );
}

float IconGrid::desiredScrollPositionForFocus(
    int focusedIndex
) const {
    if (m_displayCount <= 0)
        return 0.f;

    return std::clamp(
        static_cast<float>(focusedIndex),
        0.f,
        maxScrollPosition()
    );
}

void IconGrid::updateDisplayCount() {
    m_displayIndices.clear();
    m_displayIndices.reserve(m_allIcons.size());

    for (int i = 0; i < static_cast<int>(m_allIcons.size()); ++i) {
        const auto& icon = m_allIcons[i];
        if (!icon || icon->titleId() == 0)
            continue;

        const bool isApplication =
            m_applicationTitleIds.find(icon->titleId()) !=
            m_applicationTitleIds.end();

        if (isApplication == m_showApplications)
            m_displayIndices.push_back(i);
    }

    m_displayCount = static_cast<int>(m_displayIndices.size());

    if (m_displayCount <= 0) {
        m_scrollPosition = 0.f;
    } else {
        m_scrollPosition = std::clamp(
            m_scrollPosition,
            0.f,
            maxScrollPosition()
        );
    }
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
    // Logical cell values are kept for compatibility with the existing API.
    // Actual cover geometry is distance-based in layoutAtScrollPosition().
    m_cellW = kSelectedIconSize;
    m_cellH = kSelectedIconSize;
    m_padX = 0.f;

    updateDisplayCount();

    // V10: originX is the left edge of the centre slot itself.
    m_originX =
        m_rect.x +
        m_rect.width * 0.5f -
        kSelectedIconSize * 0.5f;

    // Covers share a baseline: smaller neighbours sit lower while the centre
    // cover rises upward and dominates the composition.
    m_originY = kCarouselBaselineY - kSelectedIconSize;

    rebuildFocusRow();
}

void IconGrid::setPage(int page) {
    (void)page;
    layoutCarousel();
}

void IconGrid::rebuildFocusRow() {
    nxui::Widget* previousFocus = m_focus.current();

    clearChildren();

    // Hide everything first; only the active category is re-added below.
    for (auto& icon : m_allIcons) {
        if (icon)
            icon->setVisible(false);
    }

    std::vector<nxui::Widget*> focusItems;
    focusItems.reserve(m_displayCount);

    for (int globalIndex : m_displayIndices) {
        if (globalIndex < 0 ||
            globalIndex >= static_cast<int>(m_allIcons.size()))
            continue;

        auto& icon = m_allIcons[globalIndex];
        if (!icon)
            continue;

        // V10 FIX5: rebuildFocusRow() hides every icon before rebuilding the
        // active category. forceVisible() only resets the appear animation; it
        // does NOT change Widget::m_visible. Explicitly restore visibility or
        // the covers remain interactive/focusable but are never rendered.
        icon->setVisible(true);
        icon->forceVisible();
        addChild(icon);

        if (icon->isFocusable())
            focusItems.push_back(icon.get());
    }

    m_focus.setGrid(
        focusItems,
        std::max(1, static_cast<int>(focusItems.size()))
    );

    nxui::Widget* targetFocus = nullptr;
    if (previousFocus) {
        auto it = std::find(
            focusItems.begin(),
            focusItems.end(),
            previousFocus
        );
        if (it != focusItems.end())
            targetFocus = previousFocus;
    }

    if (!targetFocus && !focusItems.empty())
        targetFocus = focusItems.front();

    if (targetFocus)
        m_focus.setFocus(targetFocus);

    layoutCarousel();
}

void IconGrid::layoutCarousel() {
    if (m_displayCount <= 0)
        return;

    if (m_preserveScrollOnNextFocus) {
        m_preserveScrollOnNextFocus = false;
        layoutAtScrollPosition();
        return;
    }

    // Une navigation manette interrompt proprement
    // l'inertie tactile et recentre la rangée.
    m_touchScrolling = false;
    m_inertiaActive = false;
    m_snapActive = false;
    m_scrollVelocity = 0.f;

    const int focusedGlobal = focusedGlobalIndex();
    int focusedDisplay = displayPositionForGlobalIndex(focusedGlobal);

    if (focusedDisplay < 0 ||
        focusedDisplay >= m_displayCount)
        focusedDisplay = 0;

    // V10: controller navigation uses the same continuous scroll coordinate as
    // touch. Moving focus therefore animates both covers at once: the old hero
    // shrinks 310 -> 230 while the new one grows 230 -> 310. Repeated/held
    // left-right presses simply retarget this snap without blocking input.
    m_snapTarget = desiredScrollPositionForFocus(focusedDisplay);

    if (std::abs(m_snapTarget - m_scrollPosition) < 0.0025f) {
        m_scrollPosition = m_snapTarget;
        m_snapActive = false;
        layoutAtScrollPosition();
    } else {
        m_snapActive = true;
        layoutAtScrollPosition();
    }
}

void IconGrid::layoutAtScrollPosition() {
    if (m_displayCount <= 0)
        return;

    m_scrollPosition = std::clamp(
        m_scrollPosition,
        0.f,
        maxScrollPosition()
    );

    const int visibleSlots =
        visibleSlotCount();

    const float screenCenterX =
        m_rect.x + m_rect.width * 0.5f;

    m_originX = screenCenterX - kSelectedIconSize * 0.5f;
    m_originY = kCarouselBaselineY - kSelectedIconSize;

    m_windowStart = std::max(
        0,
        static_cast<int>(std::floor(m_scrollPosition)) -
            visibleSlots / 2
    );

    for (int displayIndex = 0;
         displayIndex < m_displayCount;
         ++displayIndex) {
        const int globalIndex = m_displayIndices[displayIndex];
        if (globalIndex < 0 ||
            globalIndex >= static_cast<int>(m_allIcons.size()))
            continue;

        auto& icon = m_allIcons[globalIndex];
        if (!icon)
            continue;

        const float logicalDistance =
            static_cast<float>(displayIndex) - m_scrollPosition;
        const float size = m_carouselFocusActive
            ? iconSizeForDistance(logicalDistance)
            : kNeighborIconSize;
        const float centerX = m_carouselFocusActive
            ? screenCenterX + carouselCenterOffset(logicalDistance)
            : screenCenterX + logicalDistance * (kNeighborIconSize + kCarouselGap);

        icon->setRect({
            centerX - size * 0.5f,
            kCarouselBaselineY - size,
            size,
            size
        });

        // Keep active-category covers renderable after any relayout/snap.
        // This also protects against a category rebuild leaving m_visible=false.
        icon->setVisible(true);
        icon->forceVisible();
    }
}

bool IconGrid::canTouchScroll() const {
    // Even a short list can be dragged: the centre slot, not the number of
    // simultaneously visible covers, is what defines selection in V10.
    return m_displayCount > 1;
}

void IconGrid::beginTouchScroll() {
    if (!canTouchScroll())
        return;

    m_touchScrolling = true;
    m_inertiaActive = false;
    m_snapActive = false;
    m_scrollVelocity = 0.f;
    m_pendingSettledFocusIndex = -1;

    // The focused item is already centred when a drag begins. Keeping the
    // offset at zero makes the item landing under screen centre become focus.
    m_touchFocusOffset = 0.f;
}

void IconGrid::dragTouchScroll(
    float deltaPixelsX
) {
    if (!m_touchScrolling ||
        !canTouchScroll())
        return;

    const float step = kNeighborIconSize + kCarouselGap;

    if (step <= 0.f)
        return;

    // Le contenu suit le doigt.
    // Doigt vers la gauche = applications suivantes.
    m_scrollPosition -=
        deltaPixelsX / step;

    m_scrollPosition = std::clamp(
        m_scrollPosition,
        0.f,
        maxScrollPosition()
    );

    layoutAtScrollPosition();
}

void IconGrid::endTouchScroll(
    float fingerVelocityPixelsPerSecond
) {
    if (!m_touchScrolling)
        return;

    m_touchScrolling = false;

    const float step = kNeighborIconSize + kCarouselGap;

    if (step <= 0.f) {
        startSnapToNearest();
        return;
    }

    const float rawVelocity =
        -fingerVelocityPixelsPerSecond /
        step;

    const float rawSpeed =
        std::abs(rawVelocity);

    // Accélération progressive :
    // un petit geste garde peu d'inertie,
    // un geste puissant parcourt plusieurs icônes.
    const float powerBoost =
        1.f +
        std::clamp(
            (rawSpeed - 1.f) * 0.18f,
            0.f,
            1.15f
        );

    m_scrollVelocity = std::clamp(
        rawVelocity * powerBoost,
        -kMaximumInertiaSpeed,
        kMaximumInertiaSpeed
    );

    if (std::abs(m_scrollVelocity) <
        kMinimumInertiaSpeed) {
        m_scrollVelocity = 0.f;
        startSnapToNearest();
    } else {
        m_inertiaActive = true;
        m_snapActive = false;
    }
}

void IconGrid::startSnapToNearest() {
    m_inertiaActive = false;
    m_scrollVelocity = 0.f;

    m_snapTarget = std::clamp(
        std::round(m_scrollPosition),
        0.f,
        maxScrollPosition()
    );

    m_snapActive = true;
}

void IconGrid::finishSnap() {
    m_scrollPosition = m_snapTarget;

    m_snapActive = false;
    m_inertiaActive = false;
    m_scrollVelocity = 0.f;

    layoutAtScrollPosition();

    const int targetDisplay =
        std::clamp(
            static_cast<int>(std::round(m_scrollPosition)),
            0,
            std::max(0, m_displayCount - 1)
        );

    m_pendingSettledFocusIndex =
        globalIndexForDisplayPosition(targetDisplay);
}

int IconGrid::consumeSettledFocusIndex() {
    const int globalIndex = m_pendingSettledFocusIndex;
    m_pendingSettledFocusIndex = -1;

    if (globalIndex < 0 ||
        globalIndex >= static_cast<int>(m_allIcons.size()) ||
        !isGlobalIndexVisible(globalIndex))
        return -1;

    if (!m_allIcons[globalIndex] ||
        !m_allIcons[globalIndex]->isFocusable())
        return -1;

    nxui::Widget* target = m_allIcons[globalIndex].get();

    if (m_focus.current() == target)
        return -1;

    m_preserveScrollOnNextFocus = true;
    m_focus.setFocus(target);

    return globalIndex;
}

int IconGrid::focusedGlobalIndex() const {
    auto* current = m_focus.current();

    if (!current)
        return -1;

    for (int globalIndex : m_displayIndices) {
        if (globalIndex >= 0 &&
            globalIndex < static_cast<int>(m_allIcons.size()) &&
            m_allIcons[globalIndex].get() == current)
            return globalIndex;
    }

    return -1;
}

bool IconGrid::focusGlobalIndex(int idx) {
    if (idx < 0 ||
        idx >= static_cast<int>(m_allIcons.size()) ||
        !isGlobalIndexVisible(idx))
        return false;

    if (!m_allIcons[idx] ||
        !m_allIcons[idx]->isFocusable())
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

    nxui::Widget* focused =
        m_focus.current();

    std::swap(
        m_allIcons[a],
        m_allIcons[b]
    );

    rebuildFocusRow();

    if (focused)
        m_focus.setFocus(focused);

    layoutCarousel();
    return true;
}

std::vector<GlossyIcon*> IconGrid::pageIcons() const {
    std::vector<GlossyIcon*> out;
    out.reserve(m_displayCount);

    for (int globalIndex : m_displayIndices) {
        if (globalIndex >= 0 &&
            globalIndex < static_cast<int>(m_allIcons.size()) &&
            m_allIcons[globalIndex])
            out.push_back(m_allIcons[globalIndex].get());
    }

    return out;
}

int IconGrid::hitTest(
    float screenX,
    float screenY
) const {
    if (!m_rect.contains(screenX, screenY))
        return -1;

    for (int globalIndex : m_displayIndices) {
        if (globalIndex < 0 ||
            globalIndex >= static_cast<int>(m_allIcons.size()) ||
            !m_allIcons[globalIndex])
            continue;

        const nxui::Rect iconRect =
            m_allIcons[globalIndex]->focusRect();

        if (iconRect.intersects(m_rect) &&
            iconRect.contains(screenX, screenY))
            return globalIndex;
    }

    return -1;
}

void IconGrid::startAppearAnimation() {
    if (m_displayCount <= 0)
        return;

    const int first =
        std::max(
            0,
            static_cast<int>(
                std::floor(m_scrollPosition)
            ) - visibleSlotCount() / 2 - 1
        );

    const int last =
        std::min(
            m_displayCount,
            first +
            visibleSlotCount() +
            2
        );

    int order = 0;

    for (int displayIndex = first;
         displayIndex < last;
         ++displayIndex) {
        const int globalIndex =
            globalIndexForDisplayPosition(displayIndex);
        if (globalIndex < 0 ||
            globalIndex >= static_cast<int>(m_allIcons.size()) ||
            !m_allIcons[globalIndex])
            continue;

        m_allIcons[globalIndex]->setVisible(true);
        m_allIcons[globalIndex]->forceVisible();
        m_allIcons[globalIndex]->startAppear(order * 0.06f);
        ++order;
    }
}

void IconGrid::startWaveTransition(
    int targetPage
) {
    (void)targetPage;

    if (m_onPageSwitched)
        m_onPageSwitched();
}

void IconGrid::onUpdate(float dt) {
    if (m_touchScrolling ||
        m_displayCount <= 0)
        return;

    if (m_inertiaActive) {
        m_scrollPosition +=
            m_scrollVelocity * dt;

        const float maximum =
            maxScrollPosition();

        if (m_scrollPosition <= 0.f) {
            m_scrollPosition = 0.f;

            if (m_scrollVelocity < 0.f)
                m_scrollVelocity = 0.f;
        } else if (
            m_scrollPosition >= maximum
        ) {
            m_scrollPosition = maximum;

            if (m_scrollVelocity > 0.f)
                m_scrollVelocity = 0.f;
        }

        m_scrollVelocity *=
            std::exp(
                -kInertiaFriction * dt
            );

        layoutAtScrollPosition();

        if (std::abs(m_scrollVelocity) <
            kMinimumInertiaSpeed) {
            startSnapToNearest();
        }

        return;
    }

    if (m_snapActive) {
        const float difference =
            m_snapTarget -
            m_scrollPosition;

        const float amount =
            std::min(
                1.f,
                kSnapSpeed * dt
            );

        m_scrollPosition +=
            difference * amount;

        layoutAtScrollPosition();

        if (std::abs(difference) <
            0.0025f) {
            finishSnap();
        }
    }
}

void IconGrid::render(
    nxui::Renderer& renderer
) {
    if (!m_visible ||
        m_opacity <= 0.f)
        return;

    renderer.pushClipRect(m_rect);

    nxui::Widget* focused =
        m_focus.current();

    for (auto& child : m_children) {
        if (child.get() != focused)
            child->render(renderer);
    }

    if (focused)
        focused->render(renderer);

    renderer.popClipRect();
}

void IconGrid::onRender(
    nxui::Renderer&
) {
}
