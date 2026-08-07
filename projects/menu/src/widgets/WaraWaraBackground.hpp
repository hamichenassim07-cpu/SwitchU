#pragma once
#include <nxui/widgets/Background.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>
#include <vector>
#include <string>
#include <cstdint>

// V8.0A direct: les anciennes implementations onUpdate/onRender presentes
// dans WaraWaraBackground.cpp deviennent weak. Le fichier
// WaraWaraBackgroundPreviewV80.cpp fournit les implementations fortes avec
// le background contextuel par jeu, sans script ni modification manuelle.
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

    // V8.0A: appel global depuis la jaquette qui vient de recevoir le focus.
    // Le vrai chargement est temporise de 350 ms dans le background lui-meme.
    static void notifySelectedGame(uint64_t titleId);

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

    // V8.0A - preview statique par Title ID.
    uint64_t m_previewRequestedTitle = 0;
    uint64_t m_previewResolvedTitle = 0;
    uint64_t m_previewPendingTitle = 0;
    uint64_t m_previewNextTitle = 0;
    float m_previewStableTimer = 0.f;
    float m_previewFade = 0.f;
    bool m_previewLoadPending = false;
    bool m_previewTransitioning = false;
    bool m_previewCurrentAvailable = false;
    bool m_previewNextAvailable = false;
    std::string m_previewPendingPath;
    nxui::Texture m_previewCurrent;
    nxui::Texture m_previewNext;
};
