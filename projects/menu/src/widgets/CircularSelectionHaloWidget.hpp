#pragma once
#include <nxui/widgets/Widget.hpp>
#include <nxui/core/Types.hpp>

class CircularSelectionHaloWidget : public nxui::Widget {
public:
    void setTargetRect(const nxui::Rect& target);

protected:
    void onContentRender(nxui::Renderer& ren) override;
};
