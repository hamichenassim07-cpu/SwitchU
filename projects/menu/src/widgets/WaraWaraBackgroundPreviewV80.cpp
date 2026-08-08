// Switch U HOME V8.0A.2 - backgrounds contextuels asynchrones.
//
// Objectif : aucune lecture SD, aucun decodage JPEG/PNG et aucun waitIdle()
// dans le thread de rendu. Le CPU decode sur un worker. Sur deko3d, l'upload
// utilise une commande GPU dediee soumise sans attendre la fin de la queue.
// Deux textures persistantes sont alternees pour les fondus.
#define SWITCHU_V80_BACKGROUND_STRONG 1
#include "WaraWaraBackground.hpp"
#include "core/DebugLog.hpp"

#include <nxui/core/ThreadPool.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::atomic<uint64_t> g_selectedGameTitle{0};

constexpr float kPreviewDebounce = 0.35f;
constexpr float kPreviewFadeDuration = 0.32f;
constexpr int kPreviewMaxWidth = 1280;
constexpr int kPreviewMaxHeight = 720;
constexpr uint64_t kGpuRetireFrames = 3;

float random01V80() {
    return (std::rand() % 1000) / 1000.f;
}

float wrapValueV80(float value, float minValue, float maxValue) {
    const float span = maxValue - minValue;
    if (span <= 0.f)
        return minValue;
    while (value < minValue)
        value += span;
    while (value > maxValue)
        value -= span;
    return value;
}

std::string titleIdHex(uint64_t titleId) {
    char buffer[17] = {};
    std::snprintf(buffer, sizeof(buffer), "%016llX",
                  static_cast<unsigned long long>(titleId));
    return buffer;
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string previewPathFor(uint64_t titleId) {
    if (titleId == 0)
        return {};

    const std::string tid = titleIdHex(titleId);
    const std::string root = "sdmc:/config/SwitchU/game_previews/";
    const std::string folder = root + tid + "/";
    const std::string candidates[] = {
        folder + "background.jpg",
        folder + "background.jpeg",
        folder + "background.png",
        root + tid + ".jpg",
        root + tid + ".jpeg",
        root + tid + ".png",
    };

    for (const auto& candidate : candidates) {
        if (fileExists(candidate))
            return candidate;
    }
    return {};
}

float smoothStep01(float value) {
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

nxui::Rect coverRect(int texW, int texH, const nxui::Rect& area) {
    if (texW <= 0 || texH <= 0)
        return area;

    const float w = static_cast<float>(texW);
    const float h = static_cast<float>(texH);
    const float scale = std::max(area.width / w, area.height / h);
    const float drawW = w * scale;
    const float drawH = h * scale;

    return {
        area.x + (area.width - drawW) * 0.5f,
        area.y + (area.height - drawH) * 0.5f,
        drawW,
        drawH
    };
}

struct DecodedPreview {
    uint64_t titleId = 0;
    std::string path;
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    bool hasAsset = false;
    bool decoded = false;
};

void resizeRgbaNearest(const uint8_t* src,
                       int sw,
                       int sh,
                       std::vector<uint8_t>& dst,
                       int dw,
                       int dh) {
    dst.resize(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u);

    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, y * sh / std::max(1, dh));
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(sw - 1, x * sw / std::max(1, dw));
            std::memcpy(
                dst.data() + (static_cast<size_t>(y) * dw + x) * 4u,
                src + (static_cast<size_t>(sy) * sw + sx) * 4u,
                4u
            );
        }
    }
}

void decodePreviewOnWorker(const std::shared_ptr<DecodedPreview>& out) {
    if (!out || out->titleId == 0)
        return;

    out->path = previewPathFor(out->titleId);
    if (out->path.empty()) {
        out->hasAsset = false;
        out->decoded = false;
        return;
    }

    out->hasAsset = true;

    int w = 0;
    int h = 0;
    int channels = 0;
    uint8_t* pixels = stbi_load(out->path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels)
            stbi_image_free(pixels);
        out->decoded = false;
        return;
    }

    const float sx = static_cast<float>(kPreviewMaxWidth) / static_cast<float>(w);
    const float sy = static_cast<float>(kPreviewMaxHeight) / static_cast<float>(h);
    const float scale = std::min(1.f, std::min(sx, sy));
    const int dw = std::max(1, static_cast<int>(std::floor(w * scale + 0.5f)));
    const int dh = std::max(1, static_cast<int>(std::floor(h * scale + 0.5f)));

    if (dw == w && dh == h) {
        out->rgba.assign(
            pixels,
            pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u
        );
    } else {
        resizeRgbaNearest(pixels, w, h, out->rgba, dw, dh);
    }

    stbi_image_free(pixels);
    out->width = dw;
    out->height = dh;
    out->decoded = !out->rgba.empty();
}

class PreviewStreamTexture {
public:
    PreviewStreamTexture() = default;
    PreviewStreamTexture(const PreviewStreamTexture&) = delete;
    PreviewStreamTexture& operator=(const PreviewStreamTexture&) = delete;

    ~PreviewStreamTexture() {
#ifdef NXUI_BACKEND_DEKO3D
        if (m_uploadInFlight)
            m_uploadFence.wait();
        if (m_gpu && m_imageMem && m_imageAllocSize > 0)
            m_gpu->freeImageMemory(m_imageAllocSize);
#endif
    }

    bool prewarm(nxui::Renderer& ren) {
        if (m_prewarmed)
            return true;

#ifdef NXUI_BACKEND_DEKO3D
        nxui::GpuDevice& gpu = ren.gpu();
        m_gpu = &gpu;

        dk::ImageLayout maxLayout;
        dk::ImageLayoutMaker{gpu.device()}
            .setFlags(0)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(kPreviewMaxWidth, kPreviewMaxHeight)
            .initialize(maxLayout);

        m_imageAllocSize = maxLayout.getSize();
        m_imageMem = gpu.allocImageMemory(m_imageAllocSize);
        if (!m_imageMem) {
            DebugLog::log("[home-preview] prewarm failed: image memory");
            m_imageAllocSize = 0;
            return false;
        }

        // Initialiser une image valide uniquement pour reserver le bloc et le
        // descriptor. Son contenu n'est jamais dessine avant le premier upload.
        m_image.initialize(maxLayout, m_imageMem, 0);

        const uint32_t stagingBytes =
            static_cast<uint32_t>(kPreviewMaxWidth * kPreviewMaxHeight * 4);
        const uint32_t stagingAlloc =
            (stagingBytes + nxui::kGpuAlign - 1) & ~(nxui::kGpuAlign - 1);

        m_stagingMem = dk::MemBlockMaker{gpu.device(), stagingAlloc}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
        m_stagingCapacity = stagingAlloc;

        if (!m_stagingMem || !m_stagingMem.getCpuAddr()) {
            DebugLog::log("[home-preview] prewarm failed: staging memory");
            return false;
        }

        constexpr uint32_t kUploadCmdBytes = 16u * 1024u;
        m_uploadCmdMem = dk::MemBlockMaker{gpu.device(), kUploadCmdBytes}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
        if (!m_uploadCmdMem) {
            DebugLog::log("[home-preview] prewarm failed: command memory");
            return false;
        }

        m_uploadCmd = dk::CmdBufMaker{gpu.device()}.create();
        m_uploadCmd.addMemory(m_uploadCmdMem, 0, kUploadCmdBytes);

        dk::ImageView initialView{m_image};
        m_descriptorSlot = ren.registerTexture(initialView);
        if (m_descriptorSlot < 0) {
            DebugLog::log("[home-preview] prewarm failed: descriptor pool");
            return false;
        }
#else
        (void)ren;
#endif

        m_prewarmed = true;
        return true;
    }

    bool upload(nxui::Renderer& ren,
                const uint8_t* rgba,
                int w,
                int h) {
        if (!rgba || w <= 0 || h <= 0 ||
            w > kPreviewMaxWidth || h > kPreviewMaxHeight)
            return false;

        if (!prewarm(ren))
            return false;

#ifdef NXUI_BACKEND_DEKO3D
        nxui::GpuDevice& gpu = ren.gpu();

        // Cette attente ne concerne QUE le precedent upload de CE buffer.
        // Le buffer est reutilise apres plusieurs frames de retraite et le
        // debounce de 350 ms, donc la fence est normalement deja signalee.
        // Aucun queue.waitIdle() global n'est utilise.
        if (m_uploadInFlight) {
            m_uploadFence.wait();
            m_uploadInFlight = false;
        }

        dk::ImageLayout layout;
        dk::ImageLayoutMaker{gpu.device()}
            .setFlags(0)
            .setFormat(DkImageFormat_RGBA8_Unorm)
            .setDimensions(w, h)
            .initialize(layout);

        if (layout.getSize() > m_imageAllocSize)
            return false;

        const uint32_t bytes = static_cast<uint32_t>(w * h * 4);
        if (bytes > m_stagingCapacity)
            return false;

        // Le bloc image reste le meme : aucune liberation/reallocation GPU.
        m_image.initialize(layout, m_imageMem, 0);
        std::memcpy(m_stagingMem.getCpuAddr(), rgba, bytes);

        m_uploadCmd.clear();
        m_uploadCmd.addMemory(m_uploadCmdMem, 0, 16u * 1024u);

        dk::ImageView view{m_image};
        m_uploadCmd.copyBufferToImage(
            {m_stagingMem.getGpuAddr(), 0, 0},
            view,
            {0, 0, 0,
             static_cast<uint32_t>(w),
             static_cast<uint32_t>(h),
             1}
        );
        m_uploadCmd.signalFence(m_uploadFence);

        // Soumission sur la meme queue avant la command-list de la frame.
        // L'ordre de queue garantit que la copie termine avant l'echantillonnage
        // de cette texture, sans bloquer le CPU.
        dk::Queue queue = gpu.queue();
        queue.submitCommands(m_uploadCmd.finishList());
        m_uploadInFlight = true;

        ren.updateTexture(m_descriptorSlot, view);
        m_width = w;
        m_height = h;
        m_valid = true;
        return true;
#else
        if (!m_sdlTexture.loadFromPixels(ren.gpu(), ren, rgba, w, h))
            return false;
        m_width = w;
        m_height = h;
        m_valid = true;
        return true;
#endif
    }

    bool valid() const { return m_valid; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    void draw(nxui::Renderer& ren,
              const nxui::Rect& dest,
              float alpha) const {
        if (!m_valid || alpha <= 0.001f)
            return;

        const nxui::Color tint =
            nxui::Color::white().withAlpha(std::clamp(alpha, 0.f, 1.f));

#ifdef NXUI_BACKEND_DEKO3D
        if (m_descriptorSlot < 0)
            return;

        const nxui::Vec2 p0{dest.x, dest.y};
        const nxui::Vec2 p1{dest.x + dest.width, dest.y};
        const nxui::Vec2 p2{dest.x + dest.width, dest.y + dest.height};
        const nxui::Vec2 p3{dest.x, dest.y + dest.height};

        ren.drawTexturedTriangle(m_descriptorSlot,
                                 p0, {0.f, 0.f},
                                 p1, {1.f, 0.f},
                                 p2, {1.f, 1.f}, tint);
        ren.drawTexturedTriangle(m_descriptorSlot,
                                 p0, {0.f, 0.f},
                                 p2, {1.f, 1.f},
                                 p3, {0.f, 1.f}, tint);
#else
        ren.drawTexture(&m_sdlTexture, dest, tint);
#endif
    }

private:
    bool m_prewarmed = false;
    bool m_valid = false;
    int m_width = 0;
    int m_height = 0;

#ifdef NXUI_BACKEND_DEKO3D
    nxui::GpuDevice* m_gpu = nullptr;
    dk::Image m_image;
    dk::UniqueMemBlock m_imageMem;
    uint32_t m_imageAllocSize = 0;

    dk::UniqueMemBlock m_stagingMem;
    uint32_t m_stagingCapacity = 0;

    dk::UniqueMemBlock m_uploadCmdMem;
    dk::UniqueCmdBuf m_uploadCmd;
    dk::Fence m_uploadFence;
    bool m_uploadInFlight = false;

    int m_descriptorSlot = -1;
#else
    nxui::Texture m_sdlTexture;
#endif
};

} // namespace

struct WaraPreviewRuntime {
    nxui::ThreadPool loader{1};
    std::future<void> decodeFuture;
    std::shared_ptr<DecodedPreview> decoding;
    std::shared_ptr<DecodedPreview> ready;
    bool decodeInFlight = false;

    uint64_t requestedTitle = 0;
    uint64_t resolvedTitle = 0;
    uint64_t transitionTargetTitle = 0;
    uint64_t nextTitle = 0;

    float stableTimer = 0.f;
    float fade = 0.f;

    bool transitioning = false;
    bool currentAvailable = false;
    bool nextAvailable = false;

    PreviewStreamTexture textures[2];
    int currentIndex = 0;
    int nextIndex = 1;
    uint64_t slotSafeAfterFrame[2] = {0, 0};
    uint64_t frameCounter = 0;

    bool prewarmAttempted = false;
    bool prewarmOk = false;
};

namespace {

std::shared_ptr<WaraPreviewRuntime> ensureRuntime(
    std::shared_ptr<WaraPreviewRuntime>& runtime) {
    if (!runtime)
        runtime = std::make_shared<WaraPreviewRuntime>();
    return runtime;
}

void pollDecodeResult(WaraPreviewRuntime& r) {
    if (!r.decodeInFlight || !r.decodeFuture.valid())
        return;

    if (r.decodeFuture.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready)
        return;

    try {
        r.decodeFuture.get();
    } catch (...) {
        if (r.decoding)
            r.decoding->decoded = false;
    }

    r.decodeInFlight = false;

    if (!r.decoding)
        return;

    if (r.decoding->titleId != r.requestedTitle) {
        DebugLog::log("[home-preview] stale decode discarded %016llX",
                      static_cast<unsigned long long>(r.decoding->titleId));
        r.decoding.reset();
        return;
    }

    r.ready = std::move(r.decoding);
}

void scheduleDecodeIfNeeded(WaraPreviewRuntime& r) {
    if (r.requestedTitle == 0 ||
        r.requestedTitle == r.resolvedTitle ||
        r.decodeInFlight ||
        r.ready ||
        r.stableTimer < kPreviewDebounce)
        return;

    r.stableTimer = 0.f;
    r.decoding = std::make_shared<DecodedPreview>();
    r.decoding->titleId = r.requestedTitle;
    const auto job = r.decoding;

    r.decodeInFlight = true;
    r.decodeFuture = r.loader.submit([job]() {
        decodePreviewOnWorker(job);
    });

    DebugLog::log("[home-preview] async decode start %016llX",
                  static_cast<unsigned long long>(r.requestedTitle));
}

void processReadyFallback(WaraPreviewRuntime& r) {
    if (!r.ready || r.transitioning)
        return;

    if (r.ready->titleId != r.requestedTitle) {
        r.ready.reset();
        return;
    }

    if (r.ready->hasAsset && r.ready->decoded &&
        (!r.prewarmAttempted || r.prewarmOk))
        return; // l'upload GPU se fait dans onRender().

    const uint64_t title = r.ready->titleId;
    const bool hadAsset = r.ready->hasAsset;
    r.ready.reset();

    r.transitionTargetTitle = title;
    r.nextAvailable = false;
    r.nextTitle = 0;
    r.fade = 0.f;

    if (r.currentAvailable) {
        r.transitioning = true;
    } else {
        r.resolvedTitle = title;
    }

    DebugLog::log(hadAsset
        ? "[home-preview] decode failed %016llX -> theme fallback"
        : "[home-preview] no asset for %016llX -> theme fallback",
        static_cast<unsigned long long>(title));
}

} // namespace

void WaraWaraBackground::notifySelectedGame(uint64_t titleId) {
    if (titleId != 0)
        g_selectedGameTitle.store(titleId, std::memory_order_relaxed);
}

void WaraWaraBackground::onUpdate(float dt) {
    // Fond historique conserve a l'identique.
    m_time += dt;
    for (auto& s : m_shapes) {
        if (m_config.layout == Layout::Floating) {
            const float top = m_rect.y - s.size - 20.f;
            const float bottom = m_rect.y +
                ((m_rect.height > 1.f) ? m_rect.height : 720.f) + s.size + 20.f;
            const float left = m_rect.x;
            const float width = (m_rect.width > 1.f) ? m_rect.width : 1280.f;
            s.pos.y -= s.speed * dt;
            s.pos.x += std::sin(m_time * 0.7f + s.phase) * s.wobble * dt;
            s.pos.x = wrapValueV80(s.pos.x,
                                   left - s.size,
                                   left + width + s.size);
            if (s.pos.y + s.size < top) {
                s.pos.y = bottom;
                s.pos.x = left + random01V80() * width;
            }
        }
        s.rotation += s.rotSpeed * dt;
    }

    auto runtime = ensureRuntime(m_previewRuntime);
    WaraPreviewRuntime& r = *runtime;
    ++r.frameCounter;

    const uint64_t selected =
        g_selectedGameTitle.load(std::memory_order_relaxed);

    if (selected != 0 && selected != r.requestedTitle) {
        r.requestedTitle = selected;
        r.stableTimer = 0.f;

        // Une image decodee pour l'ancien focus n'a plus d'interet.
        if (r.ready && r.ready->titleId != selected)
            r.ready.reset();

        DebugLog::log("[home-preview] focus -> %016llX",
                      static_cast<unsigned long long>(selected));
    }

    if (r.requestedTitle != 0 && r.requestedTitle != r.resolvedTitle)
        r.stableTimer += std::max(0.f, dt);

    pollDecodeResult(r);
    scheduleDecodeIfNeeded(r);
    processReadyFallback(r);

    if (r.transitioning) {
        r.fade = std::min(
            1.f,
            r.fade + std::max(0.f, dt) / kPreviewFadeDuration
        );

        if (r.fade >= 1.f) {
            const int oldCurrent = r.currentIndex;

            if (r.nextAvailable) {
                r.currentIndex = r.nextIndex;
                r.nextIndex = oldCurrent;
                r.currentAvailable = true;
                r.resolvedTitle = r.transitionTargetTitle;

                // L'ancien current ne sera pas reutilise pendant au moins
                // trois frames, ce qui couvre le double buffering du renderer.
                r.slotSafeAfterFrame[r.nextIndex] =
                    r.frameCounter + kGpuRetireFrames;
            } else {
                if (r.currentAvailable) {
                    r.slotSafeAfterFrame[r.currentIndex] =
                        r.frameCounter + kGpuRetireFrames;
                }
                r.currentAvailable = false;
                r.resolvedTitle = r.transitionTargetTitle;
            }

            r.nextAvailable = false;
            r.nextTitle = 0;
            r.transitionTargetTitle = 0;
            r.fade = 0.f;
            r.transitioning = false;

            // Si l'utilisateur a deja choisi un autre jeu pendant le fondu,
            // le timer a continue : son decode peut demarrer des la frame suivante.
        }
    }
}

void WaraWaraBackground::onRender(nxui::Renderer& ren) {
    // Base Switch U actuelle : toujours presente derriere la preview.
    ren.useShader(nxui::ShaderProgram::Gradient);
    nxui::FsUniforms fs = {};
    fs.useTexture = 0;
    fs.param1 = m_time;
    fs.extra[0] = m_accent.r;  fs.extra[1] = m_accent.g;
    fs.extra[2] = m_accent.b;  fs.extra[3] = m_accent.a;
    fs.extra[4] = m_secondary.r;  fs.extra[5] = m_secondary.g;
    fs.extra[6] = m_secondary.b;  fs.extra[7] = m_secondary.a;
    fs.extra[8]  = m_shapeColor.r * 2.f;
    fs.extra[9]  = m_shapeColor.g * 2.f;
    fs.extra[10] = m_shapeColor.b * 2.f;
    fs.extra[11] = m_shapeColor.a;
    ren.pushFsUniforms(fs);
    ren.drawRect(m_rect, nxui::Color::white());
    ren.flush();
    ren.useShader(nxui::ShaderProgram::Basic);

    if (m_backgroundImage.valid() && m_config.imageOpacity > 0.f) {
        ren.drawTexture(&m_backgroundImage,
                        backgroundImageRect(),
                        nxui::Color::white().withAlpha(
                            m_config.imageOpacity * m_opacity));
    }

    for (const auto& s : m_shapes)
        drawShapeWithSymmetry(ren, s);

    ren.flush();

    auto runtime = ensureRuntime(m_previewRuntime);
    WaraPreviewRuntime& r = *runtime;

    // Preallocation une seule fois au chargement du HOME. Cela retire du chemin
    // critique de selection les allocations image/staging/descriptor.
    if (!r.prewarmAttempted) {
        r.prewarmAttempted = true;
        r.prewarmOk = r.textures[0].prewarm(ren) &&
                      r.textures[1].prewarm(ren);
        DebugLog::log("[home-preview] async GPU prewarm %s",
                      r.prewarmOk ? "ok" : "failed");
    }

    // Le worker a fini : seule la copie RGBA -> staging et la soumission GPU
    // restent ici. Aucun fichier, aucun JPEG/PNG decode, aucun waitIdle global.
    if (r.prewarmOk &&
        r.ready &&
        !r.transitioning &&
        r.ready->titleId == r.requestedTitle &&
        r.ready->hasAsset &&
        r.ready->decoded &&
        r.frameCounter >= r.slotSafeAfterFrame[r.nextIndex]) {

        const auto ready = r.ready;
        r.ready.reset();

        const bool uploaded = r.textures[r.nextIndex].upload(
            ren,
            ready->rgba.data(),
            ready->width,
            ready->height
        );

        r.transitionTargetTitle = ready->titleId;
        r.fade = 0.f;

        if (uploaded) {
            r.nextAvailable = true;
            r.nextTitle = ready->titleId;
            r.transitioning = true;
            DebugLog::log("[home-preview] async upload queued %016llX (%dx%d)",
                          static_cast<unsigned long long>(ready->titleId),
                          ready->width,
                          ready->height);
        } else {
            r.nextAvailable = false;
            r.nextTitle = 0;
            if (r.currentAvailable)
                r.transitioning = true;
            else
                r.resolvedTitle = ready->titleId;
            DebugLog::log("[home-preview] async upload failed %016llX -> theme fallback",
                          static_cast<unsigned long long>(ready->titleId));
        }
    }

    const nxui::Rect area = {
        m_rect.x,
        m_rect.y,
        (m_rect.width > 1.f) ? m_rect.width : 1280.f,
        (m_rect.height > 1.f) ? m_rect.height : 720.f
    };

    float previewVisualAlpha = 0.f;

    if (r.transitioning) {
        const float t = smoothStep01(r.fade);

        if (r.currentAvailable && r.textures[r.currentIndex].valid()) {
            const float a = 1.f - t;
            if (a > 0.001f) {
                const auto& tex = r.textures[r.currentIndex];
                tex.draw(ren,
                         coverRect(tex.width(), tex.height(), area),
                         a * m_opacity);
                previewVisualAlpha =
                    std::clamp(previewVisualAlpha + a, 0.f, 1.f);
            }
        }

        if (r.nextAvailable && r.textures[r.nextIndex].valid()) {
            if (t > 0.001f) {
                const auto& tex = r.textures[r.nextIndex];
                tex.draw(ren,
                         coverRect(tex.width(), tex.height(), area),
                         t * m_opacity);
                previewVisualAlpha =
                    std::clamp(previewVisualAlpha + t, 0.f, 1.f);
            }
        }
    } else if (r.currentAvailable && r.textures[r.currentIndex].valid()) {
        const auto& tex = r.textures[r.currentIndex];
        tex.draw(ren,
                 coverRect(tex.width(), tex.height(), area),
                 m_opacity);
        previewVisualAlpha = 1.f;
    }

    // Overlay cinematographique totalement separe du flux image/GPU.
    if (previewVisualAlpha > 0.001f) {
        const float a = std::clamp(previewVisualAlpha * m_opacity, 0.f, 1.f);
        ren.drawGradientRect(
            area,
            nxui::Color(0.006f, 0.008f, 0.018f, 0.06f * a),
            nxui::Color(0.004f, 0.004f, 0.012f, 0.76f * a)
        );
        ren.drawGradientRect(
            {area.x, area.y + area.height * 0.48f,
             area.width, area.height * 0.52f},
            nxui::Color(0.005f, 0.006f, 0.015f, 0.00f),
            nxui::Color(0.003f, 0.003f, 0.010f, 0.34f * a)
        );
    }

    ren.flush();
}
