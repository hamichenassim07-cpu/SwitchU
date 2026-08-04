#include "IconGrid.hpp"
#include "GlossyIcon.hpp"
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kVisibleIcons = 4;
constexpr float kLargeIconSize = 286.f;
constexpr float kCompactIconGap = 10.f;

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
    reconfigureLayout(cols, rows, cellW, cellH, padX, padY);
}

int IconGrid::visibleSlotCount() const {
    return std::max(
        1,
        std::min(m_visibleCols, m_displayCount)
    );
}

float IconGrid::maxScrollPosition() const {
    return static_cast<float>(
        std::max(
            0,
            m_displayCount - visibleSlotCount()
        )
    );
}

float IconGrid::desiredScrollPositionForFocus(
    int focusedIndex
) const {
    if (m_displayCount <= 0)
        return 0.f;

    const int visibleSlots =
        visibleSlotCount();

    const int desiredStart =
        focusedIndex - visibleSlots / 2;

    return std::clamp(
        static_cast<float>(desiredStart),
        0.f,
        maxScrollPosition()
    );
}

void IconGrid::updateDisplayCount() {
    m_displayCount = 0;

    for (const auto& icon : m_allIcons) {
        if (icon && icon->titleId() != 0)
            ++m_displayCount;
    }

    m_scrollPosition = std::clamp(
        m_scrollPosition,
        0.f,
        maxScrollPosition()
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
    m_padX = kCompactIconGap;

    updateDisplayCount();

    const int visibleSlots =
        visibleSlotCount();

    const float visibleW =
        visibleSlots * m_cellW +
        (visibleSlots - 1) * m_padX;

    m_originX =
        m_rect.x +
        (m_rect.width - visibleW) * 0.5f;

    m_originY =
        m_rect.y +
        (m_rect.height - m_cellH) * 0.5f;

    rebuildFocusRow();
}

void IconGrid::setPage(int page) {
    (void)page;
    layoutCarousel();
}

void IconGrid::rebuildFocusRow() {
    nxui::Widget* previousFocus =
        m_focus.current();

    clearChildren();

    std::vector<nxui::Widget*> focusItems;
    focusItems.reserve(m_displayCount);

    for (int i = 0; i < m_displayCount; ++i) {
        auto& icon = m_allIcons[i];

        if (!icon)
            continue;

        icon->forceVisible();
        addChild(icon);

        if (icon->isFocusable())
            focusItems.push_back(icon.get());
    }

    m_focus.setGrid(
        focusItems,
        std::max(
            1,
            static_cast<int>(focusItems.size())
        )
    );

    if (previousFocus) {
        auto it = std::find(
            focusItems.begin(),
            focusItems.end(),
            previousFocus
        );

        if (it != focusItems.end())
            m_focus.setFocus(previousFocus);
    }

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

    int focused = focusedGlobalIndex();

    if (focused < 0 ||
        focused >= m_displayCount)
        focused = 0;

    m_scrollPosition =
        desiredScrollPositionForFocus(focused);

    layoutAtScrollPosition();
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

    const float visibleW =
        visibleSlots * m_cellW +
        (visibleSlots - 1) * m_padX;

    m_originX =
        m_rect.x +
        (m_rect.width - visibleW) * 0.5f;

    m_originY =
        m_rect.y +
        (m_rect.height - m_cellH) * 0.5f;

    const float step =
        m_cellW + m_padX;

    m_windowStart = std::clamp(
        static_cast<int>(
            std::floor(m_scrollPosition)
        ),
        0,
        static_cast<int>(maxScrollPosition())
    );

    for (int i = 0; i < m_displayCount; ++i) {
        auto& icon = m_allIcons[i];

        if (!icon)
            continue;

        const float x =
            m_originX +
            (static_cast<float>(i) -
             m_scrollPosition) *
            step;

        icon->setRect({
            x,
            m_originY,
            m_cellW,
            m_cellH
        });

        icon->forceVisible();
    }
}

bool IconGrid::canTouchScroll() const {
    return m_displayCount >
           visibleSlotCount();
}

void IconGrid::beginTouchScroll() {
    if (!canTouchScroll())
        return;

    m_touchScrolling = true;
    m_inertiaActive = false;
    m_snapActive = false;
    m_scrollVelocity = 0.f;
    m_pendingSettledFocusIndex = -1;

    int focused =
        focusedGlobalIndex();

    if (focused < 0)
        focused = 0;

    m_touchFocusOffset = std::clamp(
        static_cast<float>(focused) -
            m_scrollPosition,
        0.f,
        static_cast<float>(
            visibleSlotCount() - 1
        )
    );
}

void IconGrid::dragTouchScroll(
    float deltaPixelsX
) {
    if (!m_touchScrolling ||
        !canTouchScroll())
        return;

    const float step =
        m_cellW + m_padX;

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

    const float step =
        m_cellW + m_padX;

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
    m_scrollPosition =
        m_snapTarget;

    m_snapActive = false;
    m_inertiaActive = false;
    m_scrollVelocity = 0.f;

    layoutAtScrollPosition();

    const int targetFocus =
        std::clamp(
            static_cast<int>(
                std::round(
                    m_scrollPosition +
                    m_touchFocusOffset
                )
            ),
            0,
            std::max(
                0,
                m_displayCount - 1
            )
        );

    m_pendingSettledFocusIndex =
        targetFocus;
}

int IconGrid::consumeSettledFocusIndex() {
    const int index =
        m_pendingSettledFocusIndex;

    m_pendingSettledFocusIndex = -1;

    if (index < 0 ||
        index >= m_displayCount)
        return -1;

    if (!m_allIcons[index] ||
        !m_allIcons[index]->isFocusable())
        return -1;

    nxui::Widget* target =
        m_allIcons[index].get();

    if (m_focus.current() == target)
        return -1;

    m_preserveScrollOnNextFocus = true;
    m_focus.setFocus(target);

    return index;
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
    if (idx < 0 ||
        idx >= m_displayCount)
        return false;

    if (!m_allIcons[idx] ||
        !m_allIcons[idx]->isFocusable())
        return false;

    m_focus.setFocus(
        m_allIcons[idx].get()
    );

    layoutCarousel();
    return true;
}

bool IconGrid::swapSlots(int a, int b) {
    if (a < 0 || b < 0 ||
        a >= m_displayCount ||
        b >= m_displayCount)
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

    for (int i = 0; i < m_displayCount; ++i) {
        if (m_allIcons[i])
            out.push_back(
                m_allIcons[i].get()
            );
    }

    return out;
}

int IconGrid::hitTest(
    float screenX,
    float screenY
) const {
    if (!m_rect.contains(
            screenX,
            screenY))
        return -1;

    for (int i = 0; i < m_displayCount; ++i) {
        if (!m_allIcons[i])
            continue;

        const nxui::Rect iconRect =
            m_allIcons[i]->focusRect();

        if (iconRect.intersects(m_rect) &&
            iconRect.contains(
                screenX,
                screenY))
            return i;
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
                std::floor(
                    m_scrollPosition
                )
            ) - 1
        );

    const int last =
        std::min(
            m_displayCount,
            first +
            visibleSlotCount() +
            2
        );

    int order = 0;

    for (int i = first;
         i < last;
         ++i) {
        if (!m_allIcons[i])
            continue;

        m_allIcons[i]->forceVisible();
        m_allIcons[i]->startAppear(
            order * 0.06f
        );

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
