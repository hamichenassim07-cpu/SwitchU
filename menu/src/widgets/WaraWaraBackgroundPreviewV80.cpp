// Switch U HOME V10 - preview statique immediate + MP4 H.264 one-shot.
//
// Le background du jeu est la premiere couche normale. Apres 5 secondes sur
// la meme selection, preview.mp4 est lu une seule fois puis le background
// reapparait. Le decode reste asynchrone (NVTEGRA si disponible, fallback
// logiciel), avec deux textures statiques et deux textures video reutilisees.
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

constexpr float kPreviewDebounce = 0.04f;
constexpr float kPreviewFadeDuration = 0.32f;
constexpr int kPreviewMaxWidth = 1280;
constexpr int kPreviewMaxHeight = 720;
constexpr int kVideoDefaultMaxWidth = 1280;
constexpr int kVideoDefaultMaxHeight = 720;
constexpr float kVideoDefaultMaxFps = 30.f;
constexpr int kVideoStressMaxWidth = 1920;
constexpr int kVideoStressMaxHeight = 1080;
constexpr float kVideoStressMaxFps = 60.f;
constexpr float kVideoStartDelay = 5.0f;
constexpr float kVideoVisualZoom = 1.06f;
constexpr float kVideoVerticalAnchor = 0.78f;
constexpr const char* kVideoStressFlag =
    "sdmc:/config/SwitchU/video_1080p60.flag";
constexpr const char* kVideoDisabledFlag =
    "sdmc:/config/SwitchU/video_disabled.flag";
constexpr float kVideoFadeInDuration = 0.26f;
constexpr float kVideoFadeOutDuration = 0.15f;
constexpr uint64_t kGpuRetireFrames = 3;
constexpr uint64_t kVideoGpuRetireFrames = 1;

std::atomic<bool> g_forceVideo720Profile{false};

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
    const float coverScale = std::max(area.width / w, area.height / h);
    const float scale = coverScale * kVideoVisualZoom;
    const float drawW = w * scale;
    const float drawH = h * scale;
    const float hiddenY = std::max(0.f, drawH - area.height);

    // V10: static JPG/PNG and MP4 use the exact same raised framing so there
    // is no vertical jump when the video fades in after five seconds.
    return {
        area.x + (area.width - drawW) * 0.5f,
        area.y - hiddenY * kVideoVerticalAnchor,
        drawW,
        drawH
    };
}

bool videoPlaybackEnabled() {
    return !fileExists(kVideoDisabledFlag);
}

bool videoStress1080p60Enabled() {
    return videoPlaybackEnabled() &&
           fileExists(kVideoStressFlag) &&
           !g_forceVideo720Profile.load(std::memory_order_relaxed);
}

nxui::Rect videoCoverRect(int texW, int texH, const nxui::Rect& area) {
    return coverRect(texW, texH, area);
}

uint64_t previewImageMemoryUsed(nxui::Renderer& ren) {
#ifdef NXUI_BACKEND_DEKO3D
    return ren.gpu().imageMemoryUsed();
#else
    (void)ren;
    return 0;
#endif
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
    Finished,
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

    bool hasQueuedFrames(uint64_t titleId) {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (const auto& frame : m_frames) {
            if (frame && frame->titleId == titleId)
                return true;
        }
        return false;
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
                   m_frames.size() < 2;
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
            if (result == AVERROR_EOF) {
                // V10: one-shot playback. Flush delayed decoder frames, then
                // report a clean Finished state instead of seeking to frame 0.
                avcodec_send_packet(codec, nullptr);
                while (isCurrent(serial)) {
                    const int receiveResult =
                        avcodec_receive_frame(codec, decoded);
                    if (receiveResult == AVERROR(EAGAIN) ||
                        receiveResult == AVERROR_EOF)
                        break;
                    if (receiveResult < 0)
                        break;
                    if (!processDecodedFrame(decoded)) {
                        cleanup();
                        return;
                    }
                }

                cleanup();
                if (isCurrent(serial))
                    setStatusIfCurrent(
                        serial,
                        titleId,
                        VideoDecodeStatus::Finished
                    );
                return;
            }
            if (result < 0)
                break;

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
        releaseGpuResources();
    }

    void configureCapacity(int maxWidth, int maxHeight) {
        if (m_prewarmed)
            return;
        m_maxWidth = std::max(1, maxWidth);
        m_maxHeight = std::max(1, maxHeight);
    }

    void releaseGpuResources() {
#ifdef NXUI_BACKEND_DEKO3D
        if (m_uploadInFlight) {
            m_uploadFence.wait();
            m_uploadInFlight = false;
        }

        if (m_gpu && m_imageMem && m_imageAllocSize > 0)
            m_gpu->freeImageMemory(m_imageAllocSize);

        // Destroy the command buffer before its backing memory.
        m_uploadCmd = nullptr;
        m_uploadCmdMem = nullptr;
        m_stagingMem = nullptr;
        m_imageMem = nullptr;
        m_imageAllocSize = 0;
        m_stagingCapacity = 0;
#endif
        m_prewarmed = false;
        m_valid = false;
        m_width = 0;
        m_height = 0;
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
            DebugLog::log("[home-preview] prewarm failed: image memory (%dx%d)",
                          m_maxWidth, m_maxHeight);
            m_imageAllocSize = 0;
            return false;
        }

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
            DebugLog::log("[home-preview] prewarm failed: staging memory (%dx%d)",
                          m_maxWidth, m_maxHeight);
            releaseGpuResources();
            return false;
        }

        constexpr uint32_t kUploadCmdBytes = 16u * 1024u;
        m_uploadCmdMem = dk::MemBlockMaker{gpu.device(), kUploadCmdBytes}
            .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
            .create();
        if (!m_uploadCmdMem) {
            DebugLog::log("[home-preview] prewarm failed: command memory");
            releaseGpuResources();
            return false;
        }

        m_uploadCmd = dk::CmdBufMaker{gpu.device()}.create();
        m_uploadCmd.addMemory(m_uploadCmdMem, 0, kUploadCmdBytes);

        dk::ImageView initialView{m_image};
        if (m_descriptorSlot < 0)
            m_descriptorSlot = ren.registerTexture(initialView);
        else
            ren.updateTexture(m_descriptorSlot, initialView);

        if (m_descriptorSlot < 0) {
            DebugLog::log("[home-preview] prewarm failed: descriptor pool");
            releaseGpuResources();
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
    bool prewarmed() const { return m_prewarmed; }
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

    // Keep descriptor slots across lock/unlock cycles: Renderer has no
    // unregister API, so the existing slot is updated when memory returns.
    int m_descriptorSlot = -1;
#else
    nxui::Texture m_sdlTexture;
#endif
};

} // namespace

struct WaraPreviewRuntime {
    nxui::ThreadPool loader{1};
    nxui::GpuDevice* gpu = nullptr;
    std::future<void> decodeFuture;
    std::shared_ptr<DecodedPreview> decoding;
    std::shared_ptr<DecodedPreview> ready;
    bool decodeInFlight = false;

    uint64_t requestedTitle = 0;
    uint64_t resolvedTitle = 0;
    uint64_t transitionTargetTitle = 0;
    uint64_t nextTitle = 0;

    float stableTimer = 0.f;
    // V10: independent selection timer. Unlike stableTimer, it keeps running
    // after the static background has resolved so video can start at T+5 s.
    float selectionTimer = 0.f;
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

    PreviewStreamTexture videoTextures[2];
    int videoCurrentIndex = 0;
    uint64_t videoSlotSafeAfterFrame[2] = {0, 0};
    bool videoPrewarmAttempted = false;
    bool videoPrewarmOk = false;
    bool videoFallbackRequired = false;
    bool videoDisabledLogged = false;

    // One-shot state is reset only when selection changes.
    bool videoAttemptedForSelection = false;
    bool videoDecoderFinished = false;
    bool videoFinishedForSelection = false;
    bool videoReleaseWhenHidden = false;
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

    // V10: the still image is the normal first stage, not a video fallback.

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
        r.resolvedTitle != r.requestedTitle ||
        r.videoAttemptedForSelection ||
        r.selectionTimer < kVideoStartDelay)
        return;

    // One attempt per selection. Returning to the same title later resets this
    // flag because the selection itself changed away and back again.
    r.videoAttemptedForSelection = true;
    r.videoDecoderFinished = false;
    r.videoFinishedForSelection = false;

    if (!videoPlaybackEnabled()) {
        r.videoDecoder.cancel();
        r.videoRequestedTitle = r.requestedTitle;
        r.videoFallbackRequired = true;
        r.videoFinishedForSelection = true;
        if (!r.videoDisabledLogged) {
            r.videoDisabledLogged = true;
            DebugLog::log("[home-video] disabled by %s", kVideoDisabledFlag);
        }
        return;
    }

    r.videoDisabledLogged = false;

    if (videoPathFor(r.requestedTitle).empty()) {
        r.videoDecoder.cancel();
        r.videoRequestedTitle = r.requestedTitle;
        r.videoFallbackRequired = true;
        r.videoFinishedForSelection = true;
        DebugLog::log("[home-video] no preview.mp4 for %016llX",
                      static_cast<unsigned long long>(r.requestedTitle));
        return;
    }

    r.videoFallbackRequired = false;
    r.videoRequestedTitle = r.requestedTitle;
    r.videoStatusLoggedTitle = 0;
    r.videoStatusLogged = VideoDecodeStatus::Idle;
    r.videoDecoder.request(r.requestedTitle);
    DebugLog::log("[home-video] request after %.1fs %016llX",
                  kVideoStartDelay,
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

    if (status == VideoDecodeStatus::Finished) {
        if (title == r.requestedTitle) {
            r.videoDecoderFinished = true;
            DebugLog::log("[home-video] finished once %016llX",
                          static_cast<unsigned long long>(title));
        }
    } else if (status == VideoDecodeStatus::NoFile) {
        r.videoFallbackRequired = true;
        r.videoFinishedForSelection = true;
        r.videoReleaseWhenHidden = true;
        DebugLog::log("[home-video] no preview.mp4 for %016llX",
                      static_cast<unsigned long long>(title));
    } else if (status == VideoDecodeStatus::Failed) {
        r.videoFallbackRequired = true;
        r.videoFinishedForSelection = true;
        r.videoReleaseWhenHidden = true;
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

#ifdef SWITCHU_V81_FFMPEG
    // If both MP4 and static fallback are unavailable, do not leave the
    // previous game's video pool resident behind the theme background.
    if (r.videoFallbackRequired &&
        r.gpu &&
        (r.videoPrewarmOk || r.videoHasCurrent)) {
        r.gpu->waitIdle();
        for (auto& texture : r.videoTextures)
            texture.releaseGpuResources();
        r.videoPrewarmAttempted = false;
        r.videoPrewarmOk = false;
        r.videoHasCurrent = false;
        r.videoCurrentTitle = 0;
        r.videoOpacity = 0.f;
        r.videoTargetOpacity = 0.f;
        DebugLog::log("[home-video] fallback has no static asset; GPU memory released");
    }
#endif

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
    // V10 also accepts 0 so an empty Applications category can explicitly
    // return the HOME to its theme background.
    g_selectedGameTitle.store(titleId, std::memory_order_relaxed);
}

void WaraWaraBackground::setPreviewActive(bool active) {
    if (m_previewActive == active)
        return;

    m_previewActive = active;

    if (!m_previewRuntime) {
        if (active)
            g_forceVideo720Profile.store(false, std::memory_order_relaxed);
        return;
    }

    WaraPreviewRuntime& r = *m_previewRuntime;

    if (!active) {
#ifdef SWITCHU_V81_FFMPEG
        r.videoDecoder.cancel();
#endif

        // This transition occurs once when the lockscreen takes ownership of
        // the screen. Waiting here is intentional: afterwards the lockscreen
        // gets a clean GPU budget and no HOME texture remains in flight.
        if (r.gpu)
            r.gpu->waitIdle();

        for (auto& texture : r.textures)
            texture.releaseGpuResources();

#ifdef SWITCHU_V81_FFMPEG
        for (auto& texture : r.videoTextures)
            texture.releaseGpuResources();
#endif

        r.ready.reset();
        r.decoding.reset();
        r.requestedTitle = 0;
        r.resolvedTitle = 0;
        r.transitionTargetTitle = 0;
        r.nextTitle = 0;
        r.stableTimer = 0.f;
        r.selectionTimer = 0.f;
        r.fade = 0.f;
        r.transitioning = false;
        r.currentAvailable = false;
        r.nextAvailable = false;
        r.currentIndex = 0;
        r.nextIndex = 1;
        r.prewarmAttempted = false;
        r.prewarmOk = false;

#ifdef SWITCHU_V81_FFMPEG
        r.videoRequestedTitle = 0;
        r.videoCurrentTitle = 0;
        r.videoStatusLoggedTitle = 0;
        r.videoStatusLogged = VideoDecodeStatus::Idle;
        r.videoPendingFrame.reset();
        r.videoFrameTimer = 0.f;
        r.videoOpacity = 0.f;
        r.videoTargetOpacity = 0.f;
        r.videoHasCurrent = false;
        r.videoCurrentIndex = 0;
        r.videoPrewarmAttempted = false;
        r.videoPrewarmOk = false;
        r.videoFallbackRequired = false;
        r.videoAttemptedForSelection = false;
        r.videoDecoderFinished = false;
        r.videoFinishedForSelection = false;
        r.videoReleaseWhenHidden = false;
#endif

        DebugLog::log("[home-preview] suspended for lockscreen; GPU preview memory released");
        return;
    }

    // Give a new HOME session one clean chance to use the optional stress
    // profile. If it cannot fit, onRender falls back to 720p automatically.
    g_forceVideo720Profile.store(false, std::memory_order_relaxed);
    r.requestedTitle = 0;
    r.resolvedTitle = 0;
    r.stableTimer = 0.f;
    r.selectionTimer = 0.f;
#ifdef SWITCHU_V81_FFMPEG
    r.videoAttemptedForSelection = false;
    r.videoDecoderFinished = false;
    r.videoFinishedForSelection = false;
    r.videoReleaseWhenHidden = false;
#endif
    DebugLog::log("[home-preview] resumed after lockscreen");
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

    if (!m_previewActive)
        return;

    auto runtime = ensureRuntime(m_previewRuntime);
    WaraPreviewRuntime& r = *runtime;
    ++r.frameCounter;

    const uint64_t selected =
        g_selectedGameTitle.load(std::memory_order_relaxed);

    if (selected != r.requestedTitle) {
        r.requestedTitle = selected;
        r.stableTimer = 0.f;
        r.selectionTimer = 0.f;

        // Cancel a half-finished static transition. The currently visible still
        // remains as a temporary bridge until the new selection is decoded.
        r.ready.reset();
        r.nextAvailable = false;
        r.nextTitle = 0;
        r.transitionTargetTitle = 0;
        r.fade = 0.f;
        r.transitioning = false;

        if (selected == 0) {
            // Empty category: gently leave the previous still and return to the
            // normal Switch U theme background.
            if (r.currentAvailable) {
                r.transitionTargetTitle = 0;
                r.nextAvailable = false;
                r.fade = 0.f;
                r.transitioning = true;
            } else {
                r.resolvedTitle = 0;
            }
        }

#ifdef SWITCHU_V81_FFMPEG
        // Any navigation immediately invalidates the previous playback. Its
        // last visible frame may fade out, but its decoder is cancelled now.
        r.videoDecoder.cancel();
        r.videoRequestedTitle = 0;
        r.videoPendingFrame.reset();
        r.videoTargetOpacity = 0.f;
        r.videoFrameTimer = 0.f;
        r.videoFallbackRequired = !videoPlaybackEnabled();
        r.videoAttemptedForSelection = false;
        r.videoDecoderFinished = false;
        r.videoFinishedForSelection = false;
        r.videoReleaseWhenHidden =
            r.videoPrewarmOk || r.videoHasCurrent;
#endif

        if (selected != 0) {
            DebugLog::log("[home-preview] focus -> %016llX",
                          static_cast<unsigned long long>(selected));
        } else {
            DebugLog::log("[home-preview] focus cleared");
        }
    }

    if (r.requestedTitle != 0) {
        r.selectionTimer += std::max(0.f, dt);
        if (r.requestedTitle != r.resolvedTitle)
            r.stableTimer += std::max(0.f, dt);
    }

    pollDecodeResult(r);
    scheduleDecodeIfNeeded(r);
    processReadyFallback(r);

#ifdef SWITCHU_V81_FFMPEG
    scheduleVideoIfNeeded(r);
    pollVideoStatus(r);

    if (r.videoFrameTimer > 0.f)
        r.videoFrameTimer = std::max(
            0.f,
            r.videoFrameTimer - std::max(0.f, dt)
        );

    if (r.videoRequestedTitle == r.requestedTitle &&
        r.videoRequestedTitle != 0 &&
        !r.videoPendingFrame &&
        (!r.videoHasCurrent || r.videoFrameTimer <= 0.f)) {
        std::shared_ptr<DecodedVideoFrame> frame;
        if (r.videoDecoder.popFrame(r.requestedTitle, frame))
            r.videoPendingFrame = std::move(frame);
    }

    // Finished means the decoder has reached the real end of the file. Wait
    // until its small queue and the duration of the final uploaded frame have
    // both drained before fading back to the still image.
    if (r.videoDecoderFinished &&
        r.videoRequestedTitle == r.requestedTitle &&
        !r.videoPendingFrame &&
        !r.videoDecoder.hasQueuedFrames(r.requestedTitle) &&
        (!r.videoHasCurrent || r.videoFrameTimer <= 0.f) &&
        !r.videoFinishedForSelection) {
        r.videoFinishedForSelection = true;
        r.videoTargetOpacity = 0.f;
        r.videoReleaseWhenHidden = true;
        DebugLog::log(
            "[home-video] final frame drained -> background %016llX",
            static_cast<unsigned long long>(r.requestedTitle)
        );
    }

    if (r.videoTargetOpacity > r.videoOpacity) {
        r.videoOpacity = std::min(
            r.videoTargetOpacity,
            r.videoOpacity + std::max(0.f, dt) / kVideoFadeInDuration
        );
    } else if (r.videoTargetOpacity < r.videoOpacity) {
        r.videoOpacity = std::max(
            r.videoTargetOpacity,
            r.videoOpacity - std::max(0.f, dt) / kVideoFadeOutDuration
        );
    }

    if (r.videoOpacity <= 0.001f &&
        r.videoTargetOpacity <= 0.001f) {
        if (r.videoCurrentTitle != r.requestedTitle ||
            r.videoFinishedForSelection)
            r.videoHasCurrent = false;

        // Release the stream pool only after the fade is invisible. This keeps
        // normal navigation smooth while returning the memory budget to HOME.
        if (r.videoReleaseWhenHidden && r.gpu) {
            r.gpu->waitIdle();
            for (auto& texture : r.videoTextures)
                texture.releaseGpuResources();

            r.videoDecoder.cancel();
            r.videoPendingFrame.reset();
            r.videoCurrentTitle = 0;
            r.videoRequestedTitle = 0;
            r.videoCurrentIndex = 0;
            r.videoPrewarmAttempted = false;
            r.videoPrewarmOk = false;
            r.videoHasCurrent = false;
            r.videoDecoderFinished = false;
            r.videoReleaseWhenHidden = false;

            DebugLog::log("[home-video] GPU stream released after playback/navigation");
        }
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

    if (!m_previewActive)
        return;

    auto runtime = ensureRuntime(m_previewRuntime);
    WaraPreviewRuntime& r = *runtime;

    r.gpu = &ren.gpu();

#ifdef SWITCHU_V81_FFMPEG
    // Allocate video resources only for a title that actually owns preview.mp4.
    if (r.videoRequestedTitle == r.requestedTitle &&
        r.videoRequestedTitle != 0 &&
        !r.videoFallbackRequired &&
        !r.videoPrewarmAttempted) {

        r.videoPrewarmAttempted = true;

        // V10 keeps the selected still alive under the MP4 so the end-of-video
        // fade can reveal it immediately. To limit GPU pressure, only the spare
        // static transition slot is released once the still is settled.
        if (r.currentAvailable &&
            !r.transitioning &&
            r.nextIndex != r.currentIndex &&
            r.textures[r.nextIndex].prewarmed()) {
            r.textures[r.nextIndex].releaseGpuResources();
            DebugLog::log(
                "[home-preview] spare static slot released before video"
            );
        }

        const bool requestedStress = videoStress1080p60Enabled();
        int capacityW = requestedStress
            ? kVideoStressMaxWidth
            : kVideoDefaultMaxWidth;
        int capacityH = requestedStress
            ? kVideoStressMaxHeight
            : kVideoDefaultMaxHeight;

        for (auto& texture : r.videoTextures)
            texture.configureCapacity(capacityW, capacityH);

        r.videoPrewarmOk =
            r.videoTextures[0].prewarm(ren) &&
            r.videoTextures[1].prewarm(ren);

        DebugLog::log(
            "[home-video] GPU stream prewarm %s profile=%s capacity=%dx%d imageUsed=%llu",
            r.videoPrewarmOk ? "ok" : "failed",
            requestedStress ? "STRESS_1080P60" : "QUALITY_720P30",
            capacityW,
            capacityH,
            static_cast<unsigned long long>(previewImageMemoryUsed(ren))
        );

        if (!r.videoPrewarmOk && requestedStress) {
            // The stress profile must never break the normal reader. Release
            // every partial 1080p allocation before retrying a smaller pool.
            ren.gpu().waitIdle();
            for (auto& texture : r.videoTextures)
                texture.releaseGpuResources();

            g_forceVideo720Profile.store(true, std::memory_order_relaxed);
            capacityW = kVideoDefaultMaxWidth;
            capacityH = kVideoDefaultMaxHeight;

            for (auto& texture : r.videoTextures)
                texture.configureCapacity(capacityW, capacityH);

            r.videoPrewarmOk =
                r.videoTextures[0].prewarm(ren) &&
                r.videoTextures[1].prewarm(ren);

            DebugLog::log(
                "[home-video] 1080p60 memory fallback -> 720p30 %s imageUsed=%llu",
                r.videoPrewarmOk ? "ok" : "failed",
                static_cast<unsigned long long>(previewImageMemoryUsed(ren))
            );

            if (r.videoPrewarmOk) {
                // Decoder may already have produced a 1080p frame. Restart it
                // after forcing the 720p profile so the pending frame fits.
                r.videoDecoder.cancel();
                r.videoPendingFrame.reset();
                r.videoCurrentTitle = 0;
                r.videoHasCurrent = false;
                r.videoOpacity = 0.f;
                r.videoTargetOpacity = 0.f;
                r.videoDecoderFinished = false;
                r.videoFinishedForSelection = false;
                r.videoDecoder.request(r.requestedTitle);
                r.videoRequestedTitle = r.requestedTitle;
            }
        }

        if (!r.videoPrewarmOk) {
            ren.gpu().waitIdle();
            for (auto& texture : r.videoTextures)
                texture.releaseGpuResources();
            r.videoDecoder.cancel();
            r.videoPendingFrame.reset();
            // Remember that this title already attempted video; otherwise the
            // next update would immediately retry the same failed allocation.
            r.videoRequestedTitle = r.requestedTitle;
            r.videoFallbackRequired = true;
            r.videoFinishedForSelection = true;
            r.videoReleaseWhenHidden = false;
            DebugLog::log(
                "[home-video] 720p GPU allocation failed -> static/theme fallback"
            );
        }
    }
#endif

    // Static preview GPU memory is lazy, but the still is now the primary
    // preview stage and may remain resident underneath video playback.
    if (r.ready &&
        r.ready->titleId == r.requestedTitle &&
        r.ready->hasAsset &&
        r.ready->decoded &&
        !r.prewarmAttempted) {

        // V10: static and video may coexist. The still is not a fallback; it
        // remains the layer underneath the one-shot MP4.
        r.prewarmAttempted = true;
        r.prewarmOk =
            r.textures[0].prewarm(ren) &&
            r.textures[1].prewarm(ren);

        if (!r.prewarmOk) {
            ren.gpu().waitIdle();
            for (auto& texture : r.textures)
                texture.releaseGpuResources();
        }

        DebugLog::log(
            "[home-preview] lazy static GPU prewarm %s imageUsed=%llu",
            r.prewarmOk ? "ok" : "failed",
            static_cast<unsigned long long>(previewImageMemoryUsed(ren))
        );
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

#ifdef SWITCHU_V81_FFMPEG
    if (r.videoPrewarmOk &&
        r.videoPendingFrame &&
        r.videoPendingFrame->titleId == r.requestedTitle) {

        int uploadIndex = -1;
        for (int i = 0; i < 2; ++i) {
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
                        r.frameCounter + kVideoGpuRetireFrames;
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

    // V10 visual integration. The media stays readable but never overwhelms
    // the HOME; the lower base is deliberately lighter than V9 and blends
    // progressively into the still/video instead of forming a black strip.
    const nxui::Color lowerBase(0.105f, 0.108f, 0.120f, 1.f);
    const float fadeStartY = area.y + area.height * 0.45f;
    const float solidStartY = area.y + area.height * 0.74f;
    const float baseAlpha = std::clamp(m_opacity, 0.f, 1.f);

    if (previewVisualAlpha > 0.001f) {
        const float a = std::clamp(previewVisualAlpha * m_opacity, 0.f, 1.f);

        // Semi-transparent dark veil across the media. Bright previews remain
        // recognizable while text and covers keep visual priority.
        ren.drawGradientRect(
            area,
            nxui::Color(0.012f, 0.010f, 0.024f, 0.14f * a),
            nxui::Color(0.015f, 0.014f, 0.026f, 0.30f * a)
        );

        // Very soft colour atmosphere; this is a tint, not an RGB effect.
        ren.drawGradientRect(
            area,
            nxui::Color(0.17f, 0.045f, 0.24f, 0.045f * a),
            nxui::Color(0.045f, 0.055f, 0.17f, 0.070f * a)
        );
    }

    // Diffuse violet/blue glows behind the carousel. Several low-alpha discs
    // approximate a broad halo without visible LED points or hard edges.
    const float glowAlpha = 0.90f * baseAlpha;
    const nxui::Vec2 violetCenter {area.x + area.width * 0.42f,
                                   area.y + area.height * 0.54f};
    const nxui::Vec2 blueCenter {area.x + area.width * 0.62f,
                                 area.y + area.height * 0.52f};

    ren.drawCircle(violetCenter, 330.f,
                   nxui::Color(0.36f, 0.08f, 0.72f, 0.012f * glowAlpha), 72);
    ren.drawCircle(violetCenter, 235.f,
                   nxui::Color(0.42f, 0.11f, 0.82f, 0.018f * glowAlpha), 64);
    ren.drawCircle(violetCenter, 145.f,
                   nxui::Color(0.48f, 0.15f, 0.92f, 0.026f * glowAlpha), 56);

    ren.drawCircle(blueCenter, 320.f,
                   nxui::Color(0.08f, 0.20f, 0.78f, 0.010f * glowAlpha), 72);
    ren.drawCircle(blueCenter, 225.f,
                   nxui::Color(0.10f, 0.28f, 0.92f, 0.017f * glowAlpha), 64);
    ren.drawCircle(blueCenter, 140.f,
                   nxui::Color(0.14f, 0.36f, 1.00f, 0.024f * glowAlpha), 56);

    // The anthracite base belongs to the HOME composition, so it remains even
    // when a title has no custom image/video.
    ren.drawGradientRect(
        {area.x, fadeStartY, area.width, solidStartY - fadeStartY},
        lowerBase.withAlpha(0.00f),
        lowerBase.withAlpha(baseAlpha)
    );

    ren.drawRect(
        {area.x, solidStartY,
         area.width, area.y + area.height - solidStartY},
        lowerBase.withAlpha(baseAlpha)
    );

    ren.flush();
}
