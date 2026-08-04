#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/focus/FocusManager.hpp>
#include <nxui/core/Types.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

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
    int iconsPerPage() const { return std::max(1, m_displayCount); }

    nxui::FocusManager& focusManager() { return m_focus; }
    const std::vector<std::shared_ptr<GlossyIcon>>& allIcons() const {
        return m_allIcons;
    }

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

    // Défilement tactile horizontal.
    bool canTouchScroll() const;
    void beginTouchScroll();
    void dragTouchScroll(float deltaPixelsX);
    void endTouchScroll(float fingerVelocityPixelsPerSecond);
    bool isTouchScrolling() const { return m_touchScrolling; }
    bool isScrollMoving() const {
        return m_touchScrolling || m_inertiaActive || m_snapActive;
    }

    // Retourne l’index à synchroniser avec le FocusManager principal.
    // -1 signifie qu’aucune synchronisation n’est nécessaire.
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

    float maxScrollPosition() const;
    float desiredScrollPositionForFocus(int focusedIndex) const;
    int visibleSlotCount() const;

    std::vector<std::shared_ptr<GlossyIcon>> m_allIcons;
    nxui::FocusManager m_focus;

    int m_visibleCols = 4;
    int m_displayCount = 0;
    int m_windowStart = 0;

    float m_cellW = 255.f;
    float m_cellH = 255.f;
    float m_padX = 12.f;
    float m_originX = 0.f;
    float m_originY = 0.f;

    // Position continue exprimée en nombre d’icônes.
    // 0 = début de la rangée, 1 = une icône plus loin, etc.
    float m_scrollPosition = 0.f;
    float m_scrollVelocity = 0.f;
    float m_snapTarget = 0.f;
    float m_touchFocusOffset = 0.f;

    bool m_touchScrolling = false;
    bool m_inertiaActive = false;
    bool m_snapActive = false;
    bool m_preserveScrollOnNextFocus = false;

    int m_pendingSettledFocusIndex = -1;

    std::function<void()> m_onPageSwitched;
};
