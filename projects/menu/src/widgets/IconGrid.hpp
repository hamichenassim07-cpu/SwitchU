#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <nxui/core/Types.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <unordered_set>
#include <cstdint>

class GlossyIcon;

class IconGrid : public nxui::Widget {
public:
    IconGrid();

    void setup(std::vector<std::shared_ptr<GlossyIcon>> icons,
               int cols, int rows,
               float cellW, float cellH,
               float padX, float padY);

    void reconfigureLayout(int cols, int rows,
                           float cellW, float cellH,
                           float padX, float padY);

    void setPage(int page);
    int currentPage() const { return 0; }
    int totalPages() const { return 1; }

    int columns() const { return std::max(1, m_displayCount); }
    int rowsPerPage() const { return 1; }

    // The streamer still needs the complete icon pool resident. Filtering is
    // visual/focus-only, so report the full list size to the texture streamer.
    int iconsPerPage() const { return std::max(1, static_cast<int>(m_allIcons.size())); }

    nxui::FocusManager& focusManager() { return m_focus; }
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons() const {
        return m_allIcons;
    }

    // V10 HOME categories. Title IDs explicitly listed as applications are
    // shown in Applications; every other installed title remains in Jeux.
    void setApplicationTitleIds(const std::vector<uint64_t>& titleIds);
    void setShowApplications(bool showApplications);
    bool showApplications() const { return m_showApplications; }
    void setSuspendedTitleId(uint64_t titleId);
    void refreshDisplayOrder();
    void setCarouselFocusActive(bool active);
    bool carouselFocusActive() const { return m_carouselFocusActive; }
    int visibleCount() const { return m_displayCount; }
    int firstVisibleGlobalIndex() const;
    int displayPositionForGlobalIndex(int globalIndex) const;
    int globalIndexForDisplayPosition(int displayPosition) const;

    std::vector<GlossyIcon*> pageIcons() const;
    int hitTest(float screenX, float screenY) const;

    int focusedGlobalIndex() const;
    bool focusGlobalIndex(int idx);
    bool swapSlots(int a, int b);

    void startAppearAnimation();

    void startWaveTransition(int targetPage);
    bool isTransitioning() const { return false; }

    void onPageSwitched(std::function<void()> cb) {
        m_onPageSwitched = std::move(cb);
    }

    bool canTouchScroll() const;
    void beginTouchScroll();
    void dragTouchScroll(float deltaPixelsX);
    void endTouchScroll(float fingerVelocityPixelsPerSecond);

    bool isTouchScrolling() const {
        return m_touchScrolling;
    }

    bool isScrollMoving() const {
        return m_touchScrolling ||
               m_inertiaActive ||
               m_snapActive;
    }

    int consumeSettledFocusIndex();

    void render(nxui::Renderer& ren) override;

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    void rebuildFocusRow();
    void layoutCarousel();
    void layoutAtScrollPosition();
    void updateDisplayCount();
    void startSnapToNearest();
    void finishSnap();
    bool isGlobalIndexVisible(int globalIndex) const;

    int visibleSlotCount() const;
    float maxScrollPosition() const;
    float desiredScrollPositionForFocus(int focusedDisplayPosition) const;

    std::vector<std::shared_ptr<GlossyIcon>> m_allIcons;
    std::vector<int> m_displayIndices;
    std::unordered_set<uint64_t> m_applicationTitleIds;
    bool m_showApplications = false;
    uint64_t m_suspendedTitleId = 0;
    bool m_carouselFocusActive = true;
    nxui::FocusManager m_focus;

    int m_visibleCols = 5;
    int m_displayCount = 0;
    int m_windowStart = 0;

    float m_cellW = 310.f;
    float m_cellH = 310.f;
    float m_padX = 0.f;
    float m_originX = 0.f;
    float m_originY = 0.f;

    float m_scrollPosition = 0.f;
    float m_scrollVelocity = 0.f;
    float m_snapTarget = 0.f;
    float m_touchFocusOffset = 0.f;

    bool m_touchScrolling = false;
    bool m_inertiaActive = false;
    bool m_snapActive = false;
    bool m_preserveScrollOnNextFocus = false;
    bool m_selectionBounceActive = false;
    bool m_entryBouncePending = false;
    float m_selectionBounceTime = 0.f;
    float m_suspendedIdleTime = 0.f;

    // Stored as a global index into m_allIcons, not a filtered display slot.
    int m_pendingSettledFocusIndex = -1;

    std::function<void()> m_onPageSwitched;
};
