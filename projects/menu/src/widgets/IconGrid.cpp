#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include "LaunchAnimation.hpp"
#include "StylisedGameCartridge.hpp"
#include "HomeCarouselStyle.hpp"
#include "HomeCarouselMotion.hpp"
#include "../core/HomeOrderStore.hpp"
#include "../core/CartridgeStyleStore.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

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

void IconGrid::setSuspendedTitleId(uint64_t titleId) {
    if (m_suspendedTitleId == titleId)
        return;
    m_suspendedTitleId = titleId;
    HomeOrderStore::instance().setSuspendedTitle(titleId);
    m_suspendedTumbleWait = 0.f;
    m_suspendedTumbleTime = 0.f;
    m_suspendedNextTumble = 22.f;
    m_suspendedTumbleActive = false;
    refreshDisplayOrder();
}

void IconGrid::refreshDisplayOrder() {
    // rebuildFocusRow already preserves the previous focus pointer whenever it
    // is still part of the active category.
    updateDisplayCount();
    rebuildFocusRow();
}

void IconGrid::setCarouselFocusActive(bool active) {
    if (m_carouselFocusActive == active)
        return;
    m_carouselFocusActive = active;
    if (!active) {
        endTouchScroll(0.f);
        m_touchOwnsSelection = false;
        m_pendingSettledFocusIndex = -1;
    }
    // Focus is independent of the selected index and of a pending snap.
    m_focusAmount.target(active ? 1.f : 0.f, active ? .21f : .17f);
    layoutAtScrollPosition();
}

void IconGrid::setShowApplications(bool showApplications) {
    if (m_showApplications == showApplications)
        return;

    m_showApplications = showApplications;
    switchu::homeui::jumpCarouselTo(m_carouselMotion, 0, m_displayCount);
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
    // The shared HOME/Music coordinate represents the item located under the
    // exact screen centre. First and last entries can therefore sit at centre.
    return switchu::homeui::carouselMaxPosition(m_displayCount);
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

    // V10.27: stable carousel order. Category filtering preserves the exact
    // underlying model/layout order; launching or suspending a title never
    // changes its position. Manual Y placement is persisted by layout slots.
    m_displayCount = static_cast<int>(m_displayIndices.size());

    switchu::homeui::clampCarouselMotion(m_carouselMotion, m_displayCount);
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

    m_visibleCols = switchu::homeui::kCarouselVisibleIcons;
    // Logical cell values are kept for compatibility with the existing API.
    // Actual cover geometry is distance-based in layoutAtScrollPosition().
    m_cellW = switchu::homeui::kCarouselSelectedSize;
    m_cellH = switchu::homeui::kCarouselSelectedSize;
    m_padX = 0.f;

    updateDisplayCount();

    // V10: originX is the left edge of the centre slot itself.
    m_originX =
        m_rect.x +
        m_rect.width * 0.5f -
        switchu::homeui::kCarouselSelectedSize * 0.5f;

    // Covers share a baseline: smaller neighbours sit lower while the centre
    // cover rises upward and dominates the composition.
    m_originY = switchu::homeui::kCarouselBaselineY - switchu::homeui::kCarouselSelectedSize;

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

    m_touchOwnsSelection = false;
    const int focusedGlobal = focusedGlobalIndex();
    int focusedDisplay = displayPositionForGlobalIndex(focusedGlobal);

    if (focusedDisplay < 0 ||
        focusedDisplay >= m_displayCount)
        focusedDisplay = 0;

    // Jeux / Applications and Music now retarget the exact same shared motion
    // engine. Repeated navigation simply changes the snap target.
    switchu::homeui::retargetCarouselSnap(
        m_carouselMotion, focusedDisplay, m_displayCount);
    layoutAtScrollPosition();
}

void IconGrid::layoutAtScrollPosition() {
    if (m_displayCount <= 0)
        return;

    m_carouselMotion.position = std::clamp(
        m_carouselMotion.position,
        0.f,
        maxScrollPosition()
    );

    const int visibleSlots =
        visibleSlotCount();

    const float screenCenterX =
        m_rect.x + m_rect.width * 0.5f;

    m_originX = screenCenterX - switchu::homeui::kCarouselSelectedSize * 0.5f;
    m_originY = switchu::homeui::kCarouselBaselineY - switchu::homeui::kCarouselSelectedSize;

    m_windowStart = std::max(
        0,
        static_cast<int>(std::floor(m_carouselMotion.position)) -
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
            static_cast<float>(displayIndex) - m_carouselMotion.position;
        float size = switchu::homeui::kCarouselNeighborSize + m_focusAmount.value() *
            (switchu::homeui::carouselIconSizeForDistance(logicalDistance) -
             switchu::homeui::kCarouselNeighborSize);

        // Interpolate the two pre-existing focus layouts. Both endpoint
        // spacings and the entire focused horizontal motion remain unchanged.
        const float restOffset = logicalDistance *
            (switchu::homeui::kCarouselNeighborSize + switchu::homeui::kCarouselGap);
        const float focusedOffset = switchu::homeui::carouselCenterOffset(logicalDistance);
        const float centerX = screenCenterX + restOffset +
            m_focusAmount.value() * (focusedOffset - restOffset);

        icon->setRect({
            centerX - size * 0.5f,
            switchu::homeui::kCarouselBaselineY - size,
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
    return switchu::homeui::canTouchCarousel(m_displayCount);
}

void IconGrid::beginTouchScroll() {
    if (!canTouchScroll())
        return;

    m_touchOwnsSelection = true;
    switchu::homeui::beginCarouselTouch(m_carouselMotion, m_displayCount);
    m_pendingSettledFocusIndex = -1;
}

void IconGrid::dragTouchScroll(float deltaPixelsX) {
    if (switchu::homeui::dragCarouselTouch(
            m_carouselMotion, deltaPixelsX, m_displayCount)) {
        layoutAtScrollPosition();
        m_pendingSettledFocusIndex = globalIndexForDisplayPosition(
            static_cast<int>(std::round(m_carouselMotion.position)));
        publishTouchSelection();
    }
}

void IconGrid::publishTouchSelection() {
    if (!m_carouselFocusActive || !m_touchSelectionCb) return;
    if (consumeSettledFocusIndex() >= 0)
        m_touchSelectionCb(m_focus.current());
}

void IconGrid::endTouchScroll(float fingerVelocityPixelsPerSecond) {
    switchu::homeui::endCarouselTouch(
        m_carouselMotion, fingerVelocityPixelsPerSecond, m_displayCount);
}

void IconGrid::finishSnap() {
    m_carouselMotion.position = m_carouselMotion.snapTarget;

    m_carouselMotion.snapActive = false;
    m_carouselMotion.inertiaActive = false;
    m_carouselMotion.velocity = 0.f;

    layoutAtScrollPosition();

    const int targetDisplay =
        std::clamp(
            static_cast<int>(std::round(m_carouselMotion.position)),
            0,
            std::max(0, m_displayCount - 1)
        );

    if (m_carouselFocusActive && m_touchOwnsSelection)
        m_pendingSettledFocusIndex = globalIndexForDisplayPosition(targetDisplay);
    m_touchOwnsSelection = false;
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
                std::floor(m_carouselMotion.position)
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
    const float safeDt = switchu::homeui::uiDelta(dt);
    if (m_focusAmount.update(safeDt)) layoutAtScrollPosition();
    m_suspendedIdleTime += safeDt;

    if (m_suspendedTitleId != 0) {
        if (m_suspendedTumbleActive) {
            m_suspendedTumbleTime += safeDt;
            constexpr float kTumbleDuration = 1.08f;
            if (m_suspendedTumbleTime >= kTumbleDuration) {
                m_suspendedTumbleActive = false;
                m_suspendedTumbleTime = 0.f;
                m_suspendedTumbleWait = 0.f;
                // Deterministic-but-varied interval: roughly 24–34 seconds.
                m_suspendedNextTumble =
                    24.f + 10.f * (0.5f + 0.5f *
                        std::sin(m_suspendedIdleTime * 0.173f + 1.41f));
            }
        } else {
            m_suspendedTumbleWait += safeDt;
            if (m_suspendedTumbleWait >= m_suspendedNextTumble) {
                m_suspendedTumbleActive = true;
                m_suspendedTumbleTime = 0.f;
            }
        }
    } else {
        m_suspendedTumbleWait = 0.f;
        m_suspendedTumbleTime = 0.f;
        m_suspendedTumbleActive = false;
    }
    if (m_carouselMotion.touchScrolling ||
        m_displayCount <= 0)
        return;

    const auto motionUpdate = switchu::homeui::updateCarouselMotion(
        m_carouselMotion, safeDt, m_displayCount);
    if (motionUpdate.changed)
        layoutAtScrollPosition();
    if (m_touchOwnsSelection && m_carouselFocusActive && motionUpdate.changed)
        m_pendingSettledFocusIndex = globalIndexForDisplayPosition(
            static_cast<int>(std::round(m_carouselMotion.position)));
    if (motionUpdate.settled)
        finishSnap();
    publishTouchSelection();

}

void IconGrid::render(
    nxui::Renderer& renderer
) {
    if (!m_visible ||
        m_opacity <= 0.f)
        return;

    renderer.pushClipRect(m_rect);

    nxui::Widget* focused = m_focus.current();

    auto renderSuspendedCartridge = [&](GlossyIcon* icon) {
        if (!icon) return;
        const nxui::Rect r = icon->focusRect();
        const float degrees = 3.14159265358979323846f / 180.f;
        float yaw = std::sin(m_suspendedIdleTime * 0.82f) * 2.0f * degrees;
        float pitch = std::sin(m_suspendedIdleTime * 0.61f + 0.7f) * 1.0f * degrees;
        float floatY = std::sin(m_suspendedIdleTime * 1.08f) * 2.0f;
        float breathe = 1.f + 0.004f * std::sin(m_suspendedIdleTime * 0.76f);

        // Rare surprise animation: a short fall/tumble and a natural return.
        // It is additive to the existing gentle idle and never loops.
        if (m_suspendedTumbleActive) {
            constexpr float kTumbleDuration = 1.08f;
            const float p = std::clamp(
                m_suspendedTumbleTime / kTumbleDuration, 0.f, 1.f);
            const float arc = std::sin(p * 3.14159265358979323846f);
            const float wobble = std::sin(p * 6.2831853071795864769f);
            pitch += 34.f * degrees * arc;
            yaw += 10.f * degrees * wobble * arc;
            floatY += 7.f * arc;
            breathe *= 1.f - 0.030f * arc;
        }

        const float cardW = r.width * 0.86f;
        const float cardH = cardW * (326.f / 286.f);
        StylisedGameCartridge::draw(
            renderer, icon->texture(), nullptr, nullptr,
            r.x + r.width * 0.5f,
            r.y + r.height * 0.5f + floatY,
            cardW, cardH,
            std::max(12.f, cardW * (18.f / 286.f)),
            std::max(12.f, cardW * (22.f / 286.f)),
            yaw, pitch, breathe, 11.f,
            CartridgeStyleStore::instance().colorFor(icon->titleId()),
            nxui::Color(0.28f, 0.62f, 1.00f, 0.20f),
            m_opacity);
    };

    auto renderOne = [&](nxui::Widget* widget) {
        if (!widget) return;
        auto* glossy = dynamic_cast<GlossyIcon*>(widget);
        if (glossy && glossy->isSuspended()) {
            renderSuspendedCartridge(glossy);
            return;
        }
        widget->render(renderer);
    };

    for (auto& child : m_children) {
        if (child.get() != focused)
            renderOne(child.get());
    }

    // The independent launch/resume animation owns the focused cartridge while
    // active, preventing a duplicate suspended card underneath it.
    if (focused && !LaunchAnimation::globalPlaying())
        renderOne(focused);

    renderer.popClipRect();
}

void IconGrid::onRender(
    nxui::Renderer&
) {
}
