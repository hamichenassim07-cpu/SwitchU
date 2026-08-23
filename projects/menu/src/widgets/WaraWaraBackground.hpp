#pragma once
#include <nxui/widgets/Background.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

// Runtime V8.1 defini dans WaraWaraBackgroundPreviewV80.cpp.
// shared_ptr permet de garder le header leger et de ne pas exposer
// les details de thread/deko3d au reste du menu.
struct WaraPreviewRuntime;

// V8.1 direct : les implementations historiques onUpdate/onRender
// presentes dans WaraWaraBackground.cpp restent weak. Le fichier
// WaraWaraBackgroundPreviewV80.cpp fournit les implementations fortes.
#if defined(__GNUC__) && !defined(SWITCHU_V80_BACKGROUND_STRONG)
#define SWITCHU_V80_BACKGROUND_WEAK __attribute__((weak))
#else
#define SWITCHU_V80_BACKGROUND_WEAK
#endif

class WaraWaraBackground : public nxui::Background {
public:
    enum class Layout {
        Floating,
        Grid,
    };

    enum class ShapeSet {
        Mixed,
        Circle,
        Triangle,
        Square,
        Diamond,
        Hexagon,
    };

    enum class Symmetry {
        None,
        MirrorHorizontal,
        MirrorVertical,
        Quad,
    };

    struct Config {
        Layout layout = Layout::Floating;
        ShapeSet shapeSet = ShapeSet::Mixed;
        Symmetry symmetry = Symmetry::None;
        int shapeCount = 30;
        int gridColumns = 14;
        int gridRows = 8;
        float spacingX = 88.f;
        float spacingY = 88.f;
        float sizeMin = 14.f;
        float sizeMax = 54.f;
        float speedMin = 6.f;
        float speedMax = 28.f;
        float wobble = 16.f;
        float opacity = 1.f;
        float rotationSpeed = 0.5f;
        bool fixedOrientation = false;
        float orientationDegrees = 0.f;
        float cornerRoundness = 0.f;
        float imageOpacity = 0.f;
        bool imageCover = true;
    };

    WaraWaraBackground();

    void setConfig(const Config& config);
    const Config& config() const { return m_config; }

    bool loadImage(nxui::GpuDevice& gpu, nxui::Renderer& ren, const std::string& path);
    void clearImage();

    // Une simple selection de jaquette suffit. Le debounce, les images et le
    // MP4 sont geres par le moteur de preview asynchrone V8.1.
    static void notifySelectedGame(uint64_t titleId);

    // V10.7: -1 = Settings/left corner, +1 = Controllers/right corner, 0 = none.
    static void notifyCornerControlFocus(int side);

    // V9: HOME previews are suspended while the lockscreen is visible.
    // This prevents video/static textures from competing with lockscreen fonts
    // and icons for the limited Deko3D image-memory budget.
    void setPreviewActive(bool active);
    bool previewActive() const { return m_previewActive; }

    void regenerate(int count = 50) override;

protected:
    SWITCHU_V80_BACKGROUND_WEAK void onUpdate(float dt) override;
    SWITCHU_V80_BACKGROUND_WEAK void onRender(nxui::Renderer& ren) override;

private:
    enum ShapeType { Circle, Triangle, Square, Diamond, Hexagon, ShapeCount };

    struct Shape {
        ShapeType type;
        nxui::Vec2  pos;
        float size;
        float speed;
        float phase;
        float wobble;
        float rotation;
        float rotSpeed;
        nxui::Color color;
        float glassAlpha;
    };

    ShapeType pickShapeType() const;
    nxui::Rect backgroundImageRect() const;
    void drawShapeWithSymmetry(nxui::Renderer& ren, const Shape& s) const;
    void drawGlassShape(nxui::Renderer& ren, const Shape& s) const;
    void drawRoundedShape(nxui::Renderer& ren, const Shape& s, const nxui::Color& c) const;

    Config m_config;
    std::vector<Shape> m_shapes;
    nxui::Texture m_backgroundImage;
    float m_time = 0.f;

    // V8.1/V9 : tout l'etat preview est encapsule ici.
    std::shared_ptr<WaraPreviewRuntime> m_previewRuntime;
    bool m_previewActive = true;
};
