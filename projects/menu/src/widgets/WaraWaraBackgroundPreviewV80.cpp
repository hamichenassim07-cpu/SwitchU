// Switch U HOME V9.0 - preview MP4 H.264 asynchrone + rendu cinematographique.
//
// La base V8.0A.2 reste intacte pour les backgrounds statiques. V8.1 ajoute
// un decodeur MP4 dans un thread dedie, une petite file de frames CPU et trois
// textures GPU persistantes. Le thread HOME ne lit ni ne decode la video et
// ne fait aucun queue.waitIdle() pour une frame de preview.
#define SWITCHU_V80_BACKGROUND_STRONG 1
#include "WaraWaraBackground.hpp"
#include "core/DebugLog.hpp"

#include <nxui/core/ThreadPool.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef SWITCHU_V81_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}
#endif

namespace {

std::atomic<uint64_t> g_selectedGameTitle{0};

constexpr float kPreviewDebounce = 0.12f;
constexpr float kPreviewFadeDuration = 0.32f;
constexpr int kPreviewMaxWidth = 1280;
constexpr int kPreviewMaxHeight = 720;
constexpr int kVideoDefaultMaxWidth = 1280;
constexpr int kVideoDefaultMaxHeight = 720;
constexpr float kVideoDefaultMaxFps = 30.f;
constexpr int kVideoStressMaxWidth = 1920;
constexpr int kVideoStressMaxHeight = 1080;
constexpr float kVideoStressMaxFps = 60.f;
constexpr float kVideoVisualZoom = 1.06f;
constexpr float kVideoVerticalAnchor = 0.62f;
constexpr const char* kVideoStressFlag =
    "sdmc:/config/SwitchU/video_1080p60.flag";
constexpr float kVideoFadeInDuration = 0.26f;
constexpr float kVideoFadeOutDuration = 0.15f;
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

std::string videoPathFor(uint64_t titleId) {
    if (titleId == 0)
        return {};

    const std::string tid = titleIdHex(titleId);
    const std::string root = "sdmc:/config/SwitchU/game_previews/";
    const std::string folder = root + tid + "/";
    const std::string candidates[] = {
        folder + "preview.mp4",
        root + tid + ".mp4",
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

bool videoStress1080p60Enabled() {
    return fileExists(kVideoStressFlag);
}

nxui::Rect videoCoverRect(int texW, int texH, const nxui::Rect& area) {
    if (texW <= 0 || texH <= 0)
        return area;

    const float w = static_cast<float>(texW);
    const float h = static_cast<float>(texH);
    const float coverScale = std::max(area.width / w, area.height / h);
    const float scale = coverScale * kVideoVisualZoom;
    const float drawW = w * scale;
    const float drawH = h * scale;
    const float hiddenY = std::max(0.f, drawH - area.height);

    return {
        area.x + (area.width - drawW) * 0.5f,
        area.y - hiddenY * kVideoVerticalAnchor,
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

#ifdef SWITCHU_V81_FFMPEG

enum class VideoDecodeStatus : int {
    Idle = 0,
    Opening,
    Playing,
    NoFile,
    Failed,
};

std::string ffmpegErrorText(int errorCode) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0)
        std::snprintf(buffer, sizeof(buffer), "ffmpeg error %d", errorCode);
    return buffer;
}

struct DecodedVideoFrame {
    uint64_t serial = 0;
    uint64_t titleId = 0;
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    float duration = 1.f / 24.f;
};

class Mp4PreviewDecoder {
public:
    Mp4PreviewDecoder() : m_worker([this]() { workerLoop(); }) {}

    Mp4PreviewDecoder(const Mp4PreviewDecoder&) = delete;
    Mp4PreviewDecoder& operator=(const Mp4PreviewDecoder&) = delete;

    ~Mp4PreviewDecoder() {
        m_stop.store(true, std::memory_order_release);
        m_serial.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_hasRequest = false;
            m_frames.clear();
        }
        m_cv.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }

    uint64_t request(uint64_t titleId) {
        const uint64_t serial = m_serial.fetch_add(1, std::memory_order_acq_rel) + 1;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_requestSerial = serial;
            m_requestTitle = titleId;
            m_hasRequest = true;
            m_frames.clear();
        }
        m_statusTitle.store(titleId, std::memory_order_release);
        m_status.store(static_cast<int>(VideoDecodeStatus::Opening),
                       std::memory_order_release);
        m_cv.notify_all();
        return serial;
    }

    void cancel() {
        m_serial.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_hasRequest = false;
            m_frames.clear();
        }
        m_statusTitle.store(0, std::memory_order_release);
        m_status.store(static_cast<int>(VideoDecodeStatus::Idle),
                       std::memory_order_release);
        m_cv.notify_all();
    }

    bool popFrame(uint64_t titleId, std::shared_ptr<DecodedVideoFrame>& out) {
        std::lock_guard<std::mutex> lk(m_mutex);
        while (!m_frames.empty() && m_frames.front()->titleId != titleId)
            m_frames.pop_front();
        if (m_frames.empty())
            return false;
        out = std::move(m_frames.front());
        m_frames.pop_front();
        m_cv.notify_all();
        return static_cast<bool>(out);
    }

    VideoDecodeStatus status() const {
        return static_cast<VideoDecodeStatus>(
            m_status.load(std::memory_order_acquire));
    }

    uint64_t statusTitle() const {
        return m_statusTitle.load(std::memory_order_acquire);
    }

private:
    bool isCurrent(uint64_t serial) const {
        return !m_stop.load(std::memory_order_acquire) &&
               m_serial.load(std::memory_order_acquire) == serial;
    }

    void setStatusIfCurrent(uint64_t serial,
                            uint64_t titleId,
                            VideoDecodeStatus status) {
        if (!isCurrent(serial))
            return;
        m_statusTitle.store(titleId, std::memory_order_release);
        m_status.store(static_cast<int>(status), std::memory_order_release);
    }

    bool enqueueFrame(uint64_t serial,
                      const std::shared_ptr<DecodedVideoFrame>& frame) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [&]() {
            return m_stop.load(std::memory_order_acquire) ||
                   m_serial.load(std::memory_order_acquire) != serial ||
                   m_frames.size() < 3;
        });

        if (!isCurrent(serial))
            return false;

        m_frames.push_back(frame);
        lk.unlock();
        m_cv.notify_all();
        return true;
    }

    bool openDecoder(AVFormatContext* format,
                     int streamIndex,
                     const AVCodec* decoder,
                     bool hardware,
                     AVCodecContext*& codec,
                     AVBufferRef*& hwDevice) {
        if (!format || streamIndex < 0 || !decoder)
            return false;

        codec = avcodec_alloc_context3(decoder);
        if (!codec)
            return false;

        AVStream* stream = format->streams[streamIndex];
        if (avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
            avcodec_free_context(&codec);
            return false;
        }

        codec->thread_count = hardware ? 1 : 2;
        codec->thread_type = hardware ? FF_THREAD_FRAME : FF_THREAD_SLICE;

        if (hardware) {
            const int hwResult = av_hwdevice_ctx_create(
                &hwDevice,
                AV_HWDEVICE_TYPE_NVTEGRA,
                nullptr,
                nullptr,
                0
            );
            if (hwResult < 0 || !hwDevice) {
                if (hwDevice)
                    av_buffer_unref(&hwDevice);
                avcodec_free_context(&codec);
                return false;
            }

            codec->hw_device_ctx = av_buffer_ref(hwDevice);
            if (!codec->hw_device_ctx) {
                av_buffer_unref(&hwDevice);
                avcodec_free_context(&codec);
                return false;
            }

            // Le port devkitPro FFmpeg expose le pixel format NVTEGRA.
            // C'est aussi le chemin utilise par les lecteurs Deko3D Switch.
            codec->pix_fmt = AV_PIX_FMT_NVTEGRA;
        }

        const int openResult = avcodec_open2(codec, decoder, nullptr);
        if (openResult < 0) {
            avcodec_free_context(&codec);
            if (hwDevice)
                av_buffer_unref(&hwDevice);
            return false;
        }
        return true;
    }

    void decodeRequest(uint64_t serial, uint64_t titleId) {
        const std::string path = videoPathFor(titleId);
        if (path.empty()) {
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::NoFile);
            return;
        }

        AVFormatContext* format = nullptr;
        AVCodecContext* codec = nullptr;
        AVBufferRef* hwDevice = nullptr;
        AVPacket* packet = nullptr;
        AVFrame* decoded = nullptr;
        AVFrame* transferred = nullptr;
        SwsContext* sws = nullptr;

        auto cleanup = [&]() {
            if (sws)
                sws_freeContext(sws);
            if (transferred)
                av_frame_free(&transferred);
            if (decoded)
                av_frame_free(&decoded);
            if (packet)
                av_packet_free(&packet);
            if (codec)
                avcodec_free_context(&codec);
            if (hwDevice)
                av_buffer_unref(&hwDevice);
            if (format)
                avformat_close_input(&format);
        };

        // FFmpeg traite une chaine contenant ':' comme une URL/protocole.
        // "sdmc:/..." serait donc interprete comme un protocole "sdmc",
        // qui n'existe pas dans le build devkitPro. Forcer le protocole file
        // permet a l'I/O locale de transmettre ensuite "sdmc:/..." a libnx.
        const std::string ffmpegPath = "file:" + path;
        DebugLog::log("[home-video] ffmpeg open %s", ffmpegPath.c_str());

        int result = avformat_open_input(&format, ffmpegPath.c_str(), nullptr, nullptr);
        if (result < 0 || !format) {
            DebugLog::log("[home-video] avformat_open_input failed %016llX: %s (%d)",
                          static_cast<unsigned long long>(titleId),
                          ffmpegErrorText(result).c_str(), result);
            cleanup();
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
            return;
        }

        result = avformat_find_stream_info(format, nullptr);
        if (result < 0) {
            DebugLog::log("[home-video] avformat_find_stream_info failed %016llX: %s (%d)",
                          static_cast<unsigned long long>(titleId),
                          ffmpegErrorText(result).c_str(), result);
            cleanup();
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
            return;
        }

        const AVCodec* decoder = nullptr;
        const int streamIndex = av_find_best_stream(
            format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
        if (streamIndex < 0 || !decoder) {
            DebugLog::log("[home-video] no video stream/decoder %016llX: %s (%d)",
                          static_cast<unsigned long long>(titleId),
                          ffmpegErrorText(streamIndex).c_str(), streamIndex);
            cleanup();
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
            return;
        }

        AVStream* stream = format->streams[streamIndex];
        if (!stream || stream->codecpar->codec_id != AV_CODEC_ID_H264) {
            DebugLog::log("[home-video] unsupported codec %016llX codec_id=%d",
                          static_cast<unsigned long long>(titleId),
                          stream ? static_cast<int>(stream->codecpar->codec_id) : -1);
            cleanup();
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
            return;
        }

        bool hardware = openDecoder(format, streamIndex, decoder,
                                    true, codec, hwDevice);
        if (!hardware) {
            DebugLog::log("[home-video] NVTEGRA unavailable %016llX -> software fallback",
                          static_cast<unsigned long long>(titleId));
            if (codec)
                avcodec_free_context(&codec);
            if (hwDevice)
                av_buffer_unref(&hwDevice);
            if (!openDecoder(format, streamIndex, decoder,
                             false, codec, hwDevice)) {
                DebugLog::log("[home-video] software decoder open failed %016llX",
                              static_cast<unsigned long long>(titleId));
                cleanup();
                setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
                return;
            }
            DebugLog::log("[home-video] software decoder opened %016llX",
                          static_cast<unsigned long long>(titleId));
        } else {
            DebugLog::log("[home-video] NVTEGRA decoder opened %016llX",
                          static_cast<unsigned long long>(titleId));
        }

        packet = av_packet_alloc();
        decoded = av_frame_alloc();
        transferred = av_frame_alloc();
        if (!packet || !decoded || !transferred) {
            cleanup();
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
            return;
        }

        const bool stress1080p60 = videoStress1080p60Enabled();
        const int videoMaxWidth = stress1080p60
            ? kVideoStressMaxWidth
            : kVideoDefaultMaxWidth;
        const int videoMaxHeight = stress1080p60
            ? kVideoStressMaxHeight
            : kVideoDefaultMaxHeight;
        const float videoMaxFps = stress1080p60
            ? kVideoStressMaxFps
            : kVideoDefaultMaxFps;

        DebugLog::log(
            "[home-video] decode profile %s max=%dx%d@%.0f",
            stress1080p60 ? "STRESS_1080P60" : "QUALITY_720P30",
            videoMaxWidth,
            videoMaxHeight,
            videoMaxFps
        );

        AVRational guessedRate = av_guess_frame_rate(format, stream, nullptr);
        double sourceFps = 24.0;
        if (guessedRate.num > 0 && guessedRate.den > 0) {
            const double guessed = av_q2d(guessedRate);
            if (guessed > 1.0 && guessed < 240.0)
                sourceFps = guessed;
        }
        const double outputFps = std::clamp(sourceFps, 1.0,
                                             static_cast<double>(videoMaxFps));
        const float outputDuration = static_cast<float>(1.0 / outputFps);
        double sampleAccumulator = 0.0;

        (void)hardware;
        setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Playing);

        auto processDecodedFrame = [&](AVFrame* input) -> bool {
            sampleAccumulator += outputFps;
            if (sampleAccumulator + 0.0001 < sourceFps) {
                av_frame_unref(input);
                return true;
            }
            sampleAccumulator -= sourceFps;

            AVFrame* source = input;
            av_frame_unref(transferred);
            if (input->format == AV_PIX_FMT_NVTEGRA) {
                const int transferResult =
                    av_hwframe_transfer_data(transferred, input, 0);
                if (transferResult < 0) {
                    av_frame_unref(input);
                    return true;
                }
                source = transferred;
            }

            const int sourceW = source->width > 0 ? source->width : codec->width;
            const int sourceH = source->height > 0 ? source->height : codec->height;
            if (sourceW <= 0 || sourceH <= 0) {
                av_frame_unref(input);
                return true;
            }

            const float sx = static_cast<float>(videoMaxWidth) /
                             static_cast<float>(sourceW);
            const float sy = static_cast<float>(videoMaxHeight) /
                             static_cast<float>(sourceH);
            const float scale = std::min(1.f, std::min(sx, sy));
            const int outW = std::max(
                1, static_cast<int>(std::floor(sourceW * scale + 0.5f)));
            const int outH = std::max(
                1, static_cast<int>(std::floor(sourceH * scale + 0.5f)));

            sws = sws_getCachedContext(
                sws,
                sourceW,
                sourceH,
                static_cast<AVPixelFormat>(source->format),
                outW,
                outH,
                AV_PIX_FMT_RGBA,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr
            );
            if (!sws) {
                av_frame_unref(input);
                return true;
            }

            auto frame = std::make_shared<DecodedVideoFrame>();
            frame->serial = serial;
            frame->titleId = titleId;
            frame->width = outW;
            frame->height = outH;
            frame->duration = outputDuration;
            frame->rgba.resize(
                static_cast<size_t>(outW) * static_cast<size_t>(outH) * 4u);

            uint8_t* dstData[4] = {frame->rgba.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {outW * 4, 0, 0, 0};
            const int scaled = sws_scale(
                sws,
                source->data,
                source->linesize,
                0,
                sourceH,
                dstData,
                dstStride
            );

            av_frame_unref(input);
            if (scaled <= 0)
                return true;

            return enqueueFrame(serial, frame);
        };

        while (isCurrent(serial)) {
            result = av_read_frame(format, packet);
            if (result < 0) {
                // Preview = boucle. On repart au debut et on vide les buffers.
                if (av_seek_frame(format, streamIndex, 0,
                                  AVSEEK_FLAG_BACKWARD) < 0) {
                    break;
                }
                avcodec_flush_buffers(codec);
                sampleAccumulator = 0.0;
                continue;
            }

            if (packet->stream_index != streamIndex) {
                av_packet_unref(packet);
                continue;
            }

            // FFmpeg peut demander de recevoir une frame avant d'accepter le
            // paquet suivant. On ne jette donc jamais un paquet sur EAGAIN.
            while (isCurrent(serial)) {
                result = avcodec_send_packet(codec, packet);
                if (result != AVERROR(EAGAIN))
                    break;

                const int receiveResult = avcodec_receive_frame(codec, decoded);
                if (receiveResult < 0)
                    break;
                if (!processDecodedFrame(decoded)) {
                    av_packet_unref(packet);
                    cleanup();
                    return;
                }
            }

            av_packet_unref(packet);
            if (result < 0 && result != AVERROR(EAGAIN))
                continue;

            while (isCurrent(serial)) {
                result = avcodec_receive_frame(codec, decoded);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
                    break;
                if (result < 0)
                    break;
                if (!processDecodedFrame(decoded)) {
                    cleanup();
                    return;
                }
            }
        }

        cleanup();
        if (isCurrent(serial))
            setStatusIfCurrent(serial, titleId, VideoDecodeStatus::Failed);
    }

    void workerLoop() {
        while (!m_stop.load(std::memory_order_acquire)) {
            uint64_t serial = 0;
            uint64_t titleId = 0;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [&]() {
                    return m_stop.load(std::memory_order_acquire) || m_hasRequest;
                });
                if (m_stop.load(std::memory_order_acquire))
                    return;
                serial = m_requestSerial;
                titleId = m_requestTitle;
                m_hasRequest = false;
            }
            decodeRequest(serial, titleId);
        }
    }

    std::thread m_worker;
    std::atomic<bool> m_stop{false};
    std::atomic<uint64_t> m_serial{1};
    std::atomic<int> m_status{static_cast<int>(VideoDecodeStatus::Idle)};
    std::atomic<uint64_t> m_statusTitle{0};

    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_hasRequest = false;
    uint64_t m_requestSerial = 0;
    uint64_t m_requestTitle = 0;
    std::deque<std::shared_ptr<DecodedVideoFrame>> m_frames;
};

#endif // SWITCHU_V81_FFMPEG

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

    void configureCapacity(int maxWidth, int maxHeight) {
        if (m_prewarmed)
            return;
        m_maxWidth = std::max(1, maxWidth);
        m_maxHeight = std::max(1, maxHeight);
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
            .setDimensions(m_maxWidth, m_maxHeight)
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
            static_cast<uint32_t>(m_maxWidth * m_maxHeight * 4);
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
            w > m_maxWidth || h > m_maxHeight)
            return false;

        if (!prewarm(ren))
            return false;

#ifdef NXUI_BACKEND_DEKO3D
        nxui::GpuDevice& gpu = ren.gpu();

        // Cette attente ne concerne QUE le precedent upload de CE buffer.
        // Le buffer est reutilise apres plusieurs frames de retraite et le
        // debounce court, donc la fence est normalement deja signalee.
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
    int m_maxWidth = kPreviewMaxWidth;
    int m_maxHeight = kPreviewMaxHeight;

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

#ifdef SWITCHU_V81_FFMPEG
    Mp4PreviewDecoder videoDecoder;
    uint64_t videoRequestedTitle = 0;
    uint64_t videoCurrentTitle = 0;
    uint64_t videoStatusLoggedTitle = 0;
    VideoDecodeStatus videoStatusLogged = VideoDecodeStatus::Idle;
    std::shared_ptr<DecodedVideoFrame> videoPendingFrame;
    float videoFrameTimer = 0.f;
    float videoOpacity = 0.f;
    float videoTargetOpacity = 0.f;
    bool videoHasCurrent = false;

    PreviewStreamTexture videoTextures[3];
    int videoCurrentIndex = 0;
    uint64_t videoSlotSafeAfterFrame[3] = {0, 0, 0};
    bool videoPrewarmAttempted = false;
    bool videoPrewarmOk = false;
#endif
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

#ifdef SWITCHU_V81_FFMPEG
void scheduleVideoIfNeeded(WaraPreviewRuntime& r) {
    if (r.requestedTitle == 0 ||
        r.videoRequestedTitle == r.requestedTitle ||
        r.stableTimer < kPreviewDebounce)
        return;

    r.videoRequestedTitle = r.requestedTitle;
    r.videoStatusLoggedTitle = 0;
    r.videoStatusLogged = VideoDecodeStatus::Idle;
    r.videoDecoder.request(r.requestedTitle);
    DebugLog::log("[home-video] request %016llX",
                  static_cast<unsigned long long>(r.requestedTitle));
}

void pollVideoStatus(WaraPreviewRuntime& r) {
    const uint64_t title = r.videoDecoder.statusTitle();
    const VideoDecodeStatus status = r.videoDecoder.status();
    if (title == 0 ||
        (title == r.videoStatusLoggedTitle && status == r.videoStatusLogged))
        return;

    r.videoStatusLoggedTitle = title;
    r.videoStatusLogged = status;

    if (status == VideoDecodeStatus::NoFile) {
        DebugLog::log("[home-video] no preview.mp4 for %016llX",
                      static_cast<unsigned long long>(title));
    } else if (status == VideoDecodeStatus::Failed) {
        DebugLog::log("[home-video] decode failed %016llX -> static/theme fallback",
                      static_cast<unsigned long long>(title));
    }
}
#endif

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

#ifdef SWITCHU_V81_FFMPEG
        // On stoppe immediatement le decode de l'ancien jeu. Sa derniere frame
        // peut rester visible pendant un tres court fondu de sortie.
        r.videoDecoder.cancel();
        r.videoRequestedTitle = 0;
        r.videoPendingFrame.reset();
        r.videoTargetOpacity = 0.f;
        r.videoFrameTimer = 0.f;
#endif

        DebugLog::log("[home-preview] focus -> %016llX",
                      static_cast<unsigned long long>(selected));
    }

    if (r.requestedTitle != 0 && r.requestedTitle != r.resolvedTitle)
        r.stableTimer += std::max(0.f, dt);

    pollDecodeResult(r);
#ifdef SWITCHU_V81_FFMPEG
    scheduleVideoIfNeeded(r);
    pollVideoStatus(r);
#endif
    scheduleDecodeIfNeeded(r);
    processReadyFallback(r);

#ifdef SWITCHU_V81_FFMPEG
    if (r.videoFrameTimer > 0.f)
        r.videoFrameTimer = std::max(0.f, r.videoFrameTimer - std::max(0.f, dt));

    if (r.videoRequestedTitle == r.requestedTitle &&
        r.videoRequestedTitle != 0 &&
        !r.videoPendingFrame &&
        (!r.videoHasCurrent || r.videoFrameTimer <= 0.f)) {
        std::shared_ptr<DecodedVideoFrame> frame;
        if (r.videoDecoder.popFrame(r.requestedTitle, frame))
            r.videoPendingFrame = std::move(frame);
    }

    if (r.videoTargetOpacity > r.videoOpacity) {
        r.videoOpacity = std::min(
            r.videoTargetOpacity,
            r.videoOpacity + std::max(0.f, dt) / kVideoFadeInDuration);
    } else if (r.videoTargetOpacity < r.videoOpacity) {
        r.videoOpacity = std::max(
            r.videoTargetOpacity,
            r.videoOpacity - std::max(0.f, dt) / kVideoFadeOutDuration);
    }

    if (r.videoOpacity <= 0.001f &&
        r.videoTargetOpacity <= 0.001f &&
        r.videoCurrentTitle != r.requestedTitle) {
        r.videoHasCurrent = false;
    }
#endif

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

#ifdef SWITCHU_V81_FFMPEG
    if (!r.videoPrewarmAttempted) {
        r.videoPrewarmAttempted = true;
        const bool stress1080p60 = videoStress1080p60Enabled();
        const int capacityW = stress1080p60
            ? kVideoStressMaxWidth
            : kVideoDefaultMaxWidth;
        const int capacityH = stress1080p60
            ? kVideoStressMaxHeight
            : kVideoDefaultMaxHeight;
        for (auto& texture : r.videoTextures)
            texture.configureCapacity(capacityW, capacityH);
        r.videoPrewarmOk =
            r.videoTextures[0].prewarm(ren) &&
            r.videoTextures[1].prewarm(ren) &&
            r.videoTextures[2].prewarm(ren);
        DebugLog::log(
            "[home-video] GPU stream prewarm %s profile=%s capacity=%dx%d",
            r.videoPrewarmOk ? "ok" : "failed",
            stress1080p60 ? "STRESS_1080P60" : "QUALITY_720P30",
            capacityW,
            capacityH
        );
    }
#endif

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

#ifdef SWITCHU_V81_FFMPEG
    if (r.videoPrewarmOk &&
        r.videoPendingFrame &&
        r.videoPendingFrame->titleId == r.requestedTitle) {

        int uploadIndex = -1;
        for (int i = 0; i < 3; ++i) {
            if (r.videoHasCurrent && i == r.videoCurrentIndex)
                continue;
            if (r.frameCounter >= r.videoSlotSafeAfterFrame[i]) {
                uploadIndex = i;
                break;
            }
        }
        if (!r.videoHasCurrent && uploadIndex < 0 &&
            r.frameCounter >= r.videoSlotSafeAfterFrame[r.videoCurrentIndex]) {
            uploadIndex = r.videoCurrentIndex;
        }

        if (uploadIndex >= 0) {
            const auto frame = r.videoPendingFrame;
            const bool uploaded = r.videoTextures[uploadIndex].upload(
                ren,
                frame->rgba.data(),
                frame->width,
                frame->height
            );

            if (uploaded) {
                const bool changedTitle =
                    !r.videoHasCurrent || r.videoCurrentTitle != frame->titleId;
                if (r.videoHasCurrent && uploadIndex != r.videoCurrentIndex) {
                    r.videoSlotSafeAfterFrame[r.videoCurrentIndex] =
                        r.frameCounter + kGpuRetireFrames;
                }
                r.videoCurrentIndex = uploadIndex;
                r.videoCurrentTitle = frame->titleId;
                r.videoHasCurrent = true;
                r.videoFrameTimer = std::max(0.010f, frame->duration);
                if (changedTitle)
                    r.videoOpacity = 0.f;
                r.videoTargetOpacity = 1.f;
                r.videoPendingFrame.reset();
            } else {
                DebugLog::log("[home-video] GPU upload failed %016llX",
                              static_cast<unsigned long long>(frame->titleId));
                r.videoPendingFrame.reset();
            }
        }
    }
#endif

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

#ifdef SWITCHU_V81_FFMPEG
    if (r.videoHasCurrent &&
        r.videoCurrentTitle == r.requestedTitle &&
        r.videoTextures[r.videoCurrentIndex].valid() &&
        r.videoOpacity > 0.001f) {
        const auto& videoTexture = r.videoTextures[r.videoCurrentIndex];
        videoTexture.draw(
            ren,
            videoCoverRect(videoTexture.width(), videoTexture.height(), area),
            r.videoOpacity * m_opacity
        );
        previewVisualAlpha = std::max(previewVisualAlpha, r.videoOpacity);
    }
#endif

    // V9 : traitement visuel proche de la reference. La video reste lisible
    // en haut, prend une tres legere teinte violet/rose puis disparait dans
    // un socle anthracite opaque. Le filtre ne modifie jamais le MP4 source.
    const nxui::Color lowerBase(0.050f, 0.050f, 0.058f, 1.f);
    const float fadeStartY = area.y + area.height * 0.43f;
    const float solidStartY = area.y + area.height * 0.74f;
    const float baseAlpha = std::clamp(m_opacity, 0.f, 1.f);

    if (previewVisualAlpha > 0.001f) {
        const float a = std::clamp(previewVisualAlpha * m_opacity, 0.f, 1.f);

        // Voile sombre doux sur toute la preview pour garder le HUD lisible.
        ren.drawGradientRect(
            area,
            nxui::Color(0.020f, 0.012f, 0.035f, 0.10f * a),
            nxui::Color(0.018f, 0.014f, 0.028f, 0.26f * a)
        );

        // Teinte couleur volontairement discrete.
        ren.drawGradientRect(
            area,
            nxui::Color(0.20f, 0.055f, 0.24f, 0.055f * a),
            nxui::Color(0.08f, 0.025f, 0.13f, 0.085f * a)
        );
    }

    // Le socle du HOME appartient a la composition, pas seulement a la video.
    // Il reste donc present aussi pour un jeu qui n'a qu'un background.jpg
    // ou aucun asset personnalise.
    ren.drawGradientRect(
        {area.x, fadeStartY, area.width, solidStartY - fadeStartY},
        lowerBase.withAlpha(0.00f),
        lowerBase.withAlpha(baseAlpha)
    );

    // A partir d'ici l'eventuelle video n'est plus visible du tout. Ce n'est
    // pas du noir pur : le fond reste un gris-noir tres legerement releve.
    ren.drawRect(
        {area.x, solidStartY,
         area.width, area.y + area.height - solidStartY},
        lowerBase.withAlpha(baseAlpha)
    );

    ren.flush();
}
