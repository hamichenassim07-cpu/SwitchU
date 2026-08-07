#pragma once

#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace nxui {

// Small reusable CPU-projected 3D card. The geometry is genuinely rotated on
// X/Y/Z and perspective-projected, while the existing 2D batcher remains the
// final rasterisation path. A 12x12 texture mesh keeps affine UV error tiny
// without adding a new shader or a second GPU vertex format.
struct True3DCardStyle {
    Vec2 center {640.f, 360.f};
    float width = 304.f;
    float height = 304.f;
    float depth = 17.f;
    float cornerRadius = 40.f;
    float frameInset = 17.f;

    float pitch = 0.f;
    float yaw = 0.f;
    float roll = 0.f;
    float focalLength = 980.f;

    int textureGrid = 12;
    float opacity = 1.f;
    Color stageColor {0.08f, 0.58f, 1.f, 1.f};
};

struct True3DCardGeometry {
    static constexpr int MaxOutlinePoints = 32;
    std::array<Vec2, MaxOutlinePoints> frontOutline {};
    int outlineCount = 0;
    Rect bounds {};
};

class True3DCard {
private:
    struct Vec3 {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

public:
    static True3DCardGeometry calculateGeometry(const True3DCardStyle& style) {
        True3DCardGeometry geometry;
        if (style.width <= 2.f || style.height <= 2.f || style.opacity <= 0.001f)
            return geometry;

        constexpr int kSegmentsPerCorner = 6;
        constexpr int kOutlineCount = kSegmentsPerCorner * 4;
        std::array<Vec3, kOutlineCount> outerFront {};
        const float radius = std::clamp(style.cornerRadius, 0.f,
            std::min(style.width, style.height) * 0.5f);
        buildRoundedBoundary(outerFront, style.width, style.height,
                             radius, style.depth * 0.5f + 0.4f);

        std::array<Vec2, kOutlineCount> front2D {};
        for (int i = 0; i < kOutlineCount; ++i) {
            front2D[i] = project(outerFront[i], style);
            geometry.frontOutline[i] = front2D[i];
        }
        geometry.outlineCount = kOutlineCount;
        geometry.bounds = computeBounds(front2D);
        return geometry;
    }

    static True3DCardGeometry draw(Renderer& ren,
                                   const Texture* texture,
                                   const True3DCardStyle& style,
                                   const Color& textureTint = Color::white()) {
        True3DCardGeometry geometry;
        if (style.width <= 2.f || style.height <= 2.f || style.opacity <= 0.001f)
            return geometry;

        constexpr int kSegmentsPerCorner = 6;
        constexpr int kOutlineCount = kSegmentsPerCorner * 4;
        static_assert(kOutlineCount <= True3DCardGeometry::MaxOutlinePoints);

        const float alpha = clamp01(style.opacity);
        const float halfW = style.width * 0.5f;
        const float halfH = style.height * 0.5f;
        const float radius = std::clamp(style.cornerRadius, 0.f,
                                        std::min(halfW, halfH));
        const float frontZ = style.depth * 0.5f;
        const float backZ = -frontZ;

        std::array<Vec3, kOutlineCount> outerFront {};
        std::array<Vec3, kOutlineCount> outerBack {};
        std::array<Vec3, kOutlineCount> innerFront {};
        buildRoundedBoundary(outerFront, style.width, style.height,
                             radius, frontZ + 0.4f);
        buildRoundedBoundary(outerBack, style.width, style.height,
                             radius, backZ);

        const float innerW = std::max(8.f, style.width - 2.f * style.frameInset);
        const float innerH = std::max(8.f, style.height - 2.f * style.frameInset);
        const float innerRadius = std::max(4.f, radius - style.frameInset * 0.70f);
        buildRoundedBoundary(innerFront, innerW, innerH,
                             innerRadius, frontZ + 2.0f);

        std::array<Vec2, kOutlineCount> front2D {};
        std::array<Vec2, kOutlineCount> back2D {};
        std::array<Vec2, kOutlineCount> inner2D {};
        for (int i = 0; i < kOutlineCount; ++i) {
            front2D[i] = project(outerFront[i], style);
            back2D[i] = project(outerBack[i], style);
            inner2D[i] = project(innerFront[i], style);
            geometry.frontOutline[i] = front2D[i];
        }
        geometry.outlineCount = kOutlineCount;
        geometry.bounds = computeBounds(front2D);

        // Soft projected shadow. It follows the true silhouette rather than an
        // axis-aligned rectangle, but remains deliberately inexpensive.
        drawShadow(ren, back2D, alpha);

        // Opaque extruded sides establish actual thickness. Lighting is derived
        // from each local side normal after the 3D rotation.
        const Vec3 light = normalise({-0.38f, -0.58f, 0.72f});
        for (int i = 0; i < kOutlineCount; ++i) {
            const int j = (i + 1) % kOutlineCount;
            const Vec3 tangent = {
                outerFront[j].x - outerFront[i].x,
                outerFront[j].y - outerFront[i].y,
                0.f
            };
            Vec3 localNormal = normalise({tangent.y, -tangent.x, 0.f});
            Vec3 worldNormal = normalise(rotateVector(localNormal, style));
            const float lightAmount = std::clamp(
                0.26f + 0.74f * std::max(0.f, dot(worldNormal, light)),
                0.18f, 1.f);

            const Color deepGlass(0.003f, 0.006f, 0.020f, 0.96f * alpha);
            const Color coloured = mixColor(
                deepGlass,
                style.stageColor.withAlpha(0.96f * alpha),
                0.12f + 0.20f * lightAmount);
            const Color side = scaleRgb(coloured, 0.52f + 0.42f * lightAmount);

            ren.drawTriangle(back2D[i], back2D[j], front2D[j], side);
            ren.drawTriangle(back2D[i], front2D[j], front2D[i], side);
        }

        // Front glass body under the cover.
        drawFan(ren, front2D,
                mixColor(Color(0.003f, 0.007f, 0.026f, 0.96f * alpha),
                         style.stageColor.withAlpha(0.96f * alpha), 0.14f));

        // The game cover is a real perspective mesh. Twelve subdivisions per
        // axis are enough for a 270 px card while keeping the cost small.
        if (texture && texture->valid()) {
            drawTextureMesh(ren, texture, style, innerW, innerH,
                            frontZ + 1.45f,
                            std::clamp(style.textureGrid, 6, 16),
                            textureTint.withAlpha(textureTint.a * alpha));
        } else {
            drawPlaceholder(ren, style, innerW, innerH, frontZ + 1.45f, alpha);
        }

        // The projected rounded frame is drawn after the square texture mesh;
        // its inner arc masks the texture corners without a stencil shader.
        for (int i = 0; i < kOutlineCount; ++i) {
            const int j = (i + 1) % kOutlineCount;
            const float topBias = clamp01(
                0.5f - (outerFront[i].y + outerFront[j].y) /
                std::max(1.f, style.height));
            Color frame = mixColor(
                Color(0.004f, 0.009f, 0.032f, 0.91f * alpha),
                style.stageColor.withAlpha(0.91f * alpha),
                0.23f + 0.12f * topBias);
            ren.drawTriangle(front2D[i], inner2D[i], front2D[j], frame);
            ren.drawTriangle(inner2D[i], inner2D[j], front2D[j], frame);
        }

        // Crisp bevels and a restrained glass highlight make the thickness
        // readable without bringing back the strange oversized pseudo-frame.
        for (int i = 0; i < kOutlineCount; ++i) {
            const int j = (i + 1) % kOutlineCount;
            const float upper = clamp01(
                0.55f - (outerFront[i].y + outerFront[j].y) /
                std::max(1.f, style.height));
            ren.drawLine(front2D[i], front2D[j],
                         style.stageColor.withAlpha(
                             (0.66f + 0.13f * upper) * alpha), 2.4f);
            ren.drawLine(inner2D[i], inner2D[j],
                         Color(0.94f, 0.98f, 1.f,
                               (0.16f + 0.16f * upper) * alpha), 1.15f);
        }

        drawGlassSheen(ren, style, innerW, innerH, frontZ + 2.3f, alpha);
        return geometry;
    }

private:
    static float clamp01(float value) {
        return std::clamp(value, 0.f, 1.f);
    }

    static Color mixColor(const Color& a, const Color& b, float t) {
        t = clamp01(t);
        return {
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    }

    static Color scaleRgb(const Color& c, float scale) {
        return {
            std::clamp(c.r * scale, 0.f, 1.f),
            std::clamp(c.g * scale, 0.f, 1.f),
            std::clamp(c.b * scale, 0.f, 1.f),
            c.a
        };
    }

    static float dot(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static Vec3 normalise(const Vec3& v) {
        const float length = std::sqrt(std::max(0.000001f, dot(v, v)));
        return {v.x / length, v.y / length, v.z / length};
    }

    static Vec3 rotateVector(const Vec3& input,
                             const True3DCardStyle& style) {
        Vec3 p = input;

        const float cx = std::cos(style.pitch);
        const float sx = std::sin(style.pitch);
        p = {p.x, p.y * cx - p.z * sx, p.y * sx + p.z * cx};

        const float cy = std::cos(style.yaw);
        const float sy = std::sin(style.yaw);
        p = {p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy};

        const float cz = std::cos(style.roll);
        const float sz = std::sin(style.roll);
        p = {p.x * cz - p.y * sz, p.x * sz + p.y * cz, p.z};
        return p;
    }

    static Vec2 project(const Vec3& local,
                        const True3DCardStyle& style) {
        const Vec3 p = rotateVector(local, style);
        const float focal = std::max(300.f, style.focalLength);
        const float denominator = std::max(160.f, focal - p.z);
        const float perspective = focal / denominator;
        return {
            style.center.x + p.x * perspective,
            style.center.y + p.y * perspective
        };
    }

    template <std::size_t N>
    static void buildRoundedBoundary(std::array<Vec3, N>& points,
                                     float width,
                                     float height,
                                     float radius,
                                     float z) {
        constexpr int segmentsPerCorner = static_cast<int>(N / 4);
        constexpr float pi = 3.14159265358979323846f;
        const float halfW = width * 0.5f;
        const float halfH = height * 0.5f;
        const std::array<Vec2, 4> centers = {{
            { halfW - radius, -halfH + radius},
            { halfW - radius,  halfH - radius},
            {-halfW + radius,  halfH - radius},
            {-halfW + radius, -halfH + radius}
        }};
        const std::array<float, 4> starts = {{
            -pi * 0.5f, 0.f, pi * 0.5f, pi
        }};

        int index = 0;
        for (int corner = 0; corner < 4; ++corner) {
            for (int segment = 0; segment < segmentsPerCorner; ++segment) {
                const float t = static_cast<float>(segment) /
                                static_cast<float>(segmentsPerCorner);
                const float angle = starts[corner] + t * pi * 0.5f;
                points[index++] = {
                    centers[corner].x + std::cos(angle) * radius,
                    centers[corner].y + std::sin(angle) * radius,
                    z
                };
            }
        }
    }

    template <std::size_t N>
    static Rect computeBounds(const std::array<Vec2, N>& points) {
        float minX = points[0].x;
        float maxX = points[0].x;
        float minY = points[0].y;
        float maxY = points[0].y;
        for (std::size_t i = 1; i < N; ++i) {
            minX = std::min(minX, points[i].x);
            maxX = std::max(maxX, points[i].x);
            minY = std::min(minY, points[i].y);
            maxY = std::max(maxY, points[i].y);
        }
        return {minX, minY, maxX - minX, maxY - minY};
    }

    template <std::size_t N>
    static Vec2 polygonCenter(const std::array<Vec2, N>& points) {
        Vec2 center {0.f, 0.f};
        for (const Vec2& point : points) {
            center.x += point.x;
            center.y += point.y;
        }
        const float inv = 1.f / static_cast<float>(N);
        return {center.x * inv, center.y * inv};
    }

    template <std::size_t N>
    static void drawFan(Renderer& ren,
                        const std::array<Vec2, N>& points,
                        const Color& color) {
        const Vec2 center = polygonCenter(points);
        for (std::size_t i = 0; i < N; ++i)
            ren.drawTriangle(center, points[i], points[(i + 1) % N], color);
    }

    template <std::size_t N>
    static void drawShadow(Renderer& ren,
                           const std::array<Vec2, N>& points,
                           float alpha) {
        const Vec2 center = polygonCenter(points);
        for (int layer = 0; layer < 3; ++layer) {
            std::array<Vec2, N> expanded {};
            const float scale = 1.025f + static_cast<float>(layer) * 0.022f;
            const float offsetY = 11.f + static_cast<float>(layer) * 2.5f;
            for (std::size_t i = 0; i < N; ++i) {
                expanded[i] = {
                    center.x + (points[i].x - center.x) * scale + 5.f,
                    center.y + (points[i].y - center.y) * scale + offsetY
                };
            }
            drawFan(ren, expanded,
                    Color(0.001f, 0.002f, 0.010f,
                          (0.060f - static_cast<float>(layer) * 0.014f) * alpha));
        }
    }

    static void drawTextureMesh(Renderer& ren,
                                const Texture* texture,
                                const True3DCardStyle& style,
                                float width,
                                float height,
                                float z,
                                int grid,
                                const Color& tint) {
        const int slot = texture->descriptorSlot();
        if (slot < 0)
            return;

        const float left = -width * 0.5f;
        const float top = -height * 0.5f;
        for (int y = 0; y < grid; ++y) {
            const float v0 = static_cast<float>(y) / grid;
            const float v1 = static_cast<float>(y + 1) / grid;
            const float localY0 = top + height * v0;
            const float localY1 = top + height * v1;
            for (int x = 0; x < grid; ++x) {
                const float u0 = static_cast<float>(x) / grid;
                const float u1 = static_cast<float>(x + 1) / grid;
                const float localX0 = left + width * u0;
                const float localX1 = left + width * u1;

                const Vec2 p00 = project({localX0, localY0, z}, style);
                const Vec2 p10 = project({localX1, localY0, z}, style);
                const Vec2 p11 = project({localX1, localY1, z}, style);
                const Vec2 p01 = project({localX0, localY1, z}, style);

                ren.drawTexturedTriangle(
                    slot,
                    p00, {u0, v0},
                    p10, {u1, v0},
                    p11, {u1, v1}, tint);
                ren.drawTexturedTriangle(
                    slot,
                    p00, {u0, v0},
                    p11, {u1, v1},
                    p01, {u0, v1}, tint);
            }
        }
    }

    static void drawPlaceholder(Renderer& ren,
                                const True3DCardStyle& style,
                                float width,
                                float height,
                                float z,
                                float alpha) {
        const Vec2 p0 = project({-width * 0.5f, -height * 0.5f, z}, style);
        const Vec2 p1 = project({ width * 0.5f, -height * 0.5f, z}, style);
        const Vec2 p2 = project({ width * 0.5f,  height * 0.5f, z}, style);
        const Vec2 p3 = project({-width * 0.5f,  height * 0.5f, z}, style);
        const Color top = style.stageColor.withAlpha(0.72f * alpha);
        const Color bottom(0.12f, 0.04f, 0.28f, 0.86f * alpha);
        ren.drawTriangle(p0, p1, p2, top);
        ren.drawTriangle(p0, p2, p3, bottom);
    }

    static void drawGlassSheen(Renderer& ren,
                               const True3DCardStyle& style,
                               float width,
                               float height,
                               float z,
                               float alpha) {
        const float left = -width * 0.5f + 10.f;
        const float right = width * 0.5f - 10.f;
        const float top = -height * 0.5f + 8.f;
        const float bottom = top + height * 0.20f;
        const Vec2 p0 = project({left, top, z}, style);
        const Vec2 p1 = project({right, top, z}, style);
        const Vec2 p2 = project({right, bottom, z}, style);
        const Vec2 p3 = project({left, bottom, z}, style);
        const Color sheen(1.f, 1.f, 1.f, 0.045f * alpha);
        ren.drawTriangle(p0, p1, p2, sheen);
        ren.drawTriangle(p0, p2, p3, sheen.withAlpha(0.018f * alpha));
    }
};

} // namespace nxui
