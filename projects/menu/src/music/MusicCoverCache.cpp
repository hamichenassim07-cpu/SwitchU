#include "MusicCoverCache.hpp"

#include "core/DebugLog.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace switchu::menu::music {

namespace {
constexpr uint64_t kMaxEmbeddedCoverBytes = 6ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaxStandaloneCoverBytes = 12ULL * 1024ULL * 1024ULL;

bool looksLikeSupportedArtwork(const std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G' &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A)
        return true;
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF)
        return true;
    return false;
}

bool preflightArtwork(const std::vector<uint8_t>& bytes, int& w, int& h) {
    w = h = 0;
    if (!looksLikeSupportedArtwork(bytes)) return false;
    int comp = 0;
    if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp))
        return false;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    const uint64_t pixels = static_cast<uint64_t>(w) * static_cast<uint64_t>(h);
    return pixels <= 32ULL * 1024ULL * 1024ULL;
}

std::vector<uint8_t> readCoverBytes(const CoverRef& ref) {
    if (!ref.valid()) return {};
    std::ifstream file(ref.path, std::ios::binary);
    if (!file.is_open()) return {};

    uint64_t offset = 0;
    uint64_t size = 0;
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end <= 0) return {};
    const uint64_t fileBytes = static_cast<uint64_t>(end);

    if (ref.embedded) {
        if (ref.size == 0 || ref.size > kMaxEmbeddedCoverBytes) return {};
        if (ref.offset > fileBytes || ref.size > fileBytes - ref.offset) return {};
        offset = ref.offset;
        size = ref.size;
    } else {
        if (fileBytes > kMaxStandaloneCoverBytes) return {};
        size = fileBytes;
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()))) return {};
    return bytes;
}

uint64_t fnv1a64(const void* data, size_t size, uint64_t seed = 1469598103934665603ULL) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t fnv1a64(const std::string& text) {
    return fnv1a64(text.data(), text.size());
}

nxui::Color hsvToRgb(float h, float s, float v) {
    h -= std::floor(h);
    s = std::clamp(s, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);
    const float c = v * s;
    const float hp = h * 6.f;
    const float x = c * (1.f - std::abs(std::fmod(hp, 2.f) - 1.f));
    float r = 0.f, g = 0.f, b = 0.f;
    if (hp < 1.f) { r = c; g = x; }
    else if (hp < 2.f) { r = x; g = c; }
    else if (hp < 3.f) { g = c; b = x; }
    else if (hp < 4.f) { g = x; b = c; }
    else if (hp < 5.f) { r = x; b = c; }
    else { r = c; b = x; }
    const float m = v - c;
    return {r + m, g + m, b + m, 1.f};
}

// V8.4 reference-UI accent extraction.
//
// V8.3 removed the detached artwork-style worker because it performed a SECOND
// full image decode and crashed on real hardware. V8.4 keeps that safety rule:
// artwork is decoded exactly once on the render thread when the texture is first
// loaded. The same decoded RGBA buffer is used both for the GPU upload and for a
// small colour sample. There is no second decode, no background worker and no
// additional rear-sleeve texture.
struct DecodedArtwork {
    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
};

bool decodeArtworkOnce(const std::vector<uint8_t>& bytes, int maxSide,
                       DecodedArtwork& out) {
    out = {};
    int w = 0, h = 0, comp = 0;
    uint8_t* decoded = stbi_load_from_memory(bytes.data(),
                                              static_cast<int>(bytes.size()),
                                              &w, &h, &comp, 4);
    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return false;
    }

    int dw = w, dh = h;
    if (maxSide > 0 && (w > maxSide || h > maxSide)) {
        const float scale = std::min(static_cast<float>(maxSide) / static_cast<float>(w),
                                     static_cast<float>(maxSide) / static_cast<float>(h));
        dw = std::max(1, static_cast<int>(std::round(static_cast<float>(w) * scale)));
        dh = std::max(1, static_cast<int>(std::round(static_cast<float>(h) * scale)));
    }

    out.rgba.resize(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u);
    if (dw == w && dh == h) {
        std::memcpy(out.rgba.data(), decoded, out.rgba.size());
    } else {
        for (int y = 0; y < dh; ++y) {
            const int sy = std::min(h - 1, y * h / dh);
            for (int x = 0; x < dw; ++x) {
                const int sx = std::min(w - 1, x * w / dw);
                std::memcpy(out.rgba.data() +
                                (static_cast<size_t>(y) * static_cast<size_t>(dw) +
                                 static_cast<size_t>(x)) * 4u,
                            decoded +
                                (static_cast<size_t>(sy) * static_cast<size_t>(w) +
                                 static_cast<size_t>(sx)) * 4u,
                            4u);
            }
        }
    }
    stbi_image_free(decoded);
    out.width = dw;
    out.height = dh;
    return true;
}

struct Hsv {
    float h = 0.f;
    float s = 0.f;
    float v = 0.f;
};

Hsv rgbToHsv(float r, float g, float b) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    Hsv out{};
    out.v = mx;
    out.s = mx <= 1e-6f ? 0.f : d / mx;
    if (d <= 1e-6f) return out;
    if (mx == r) out.h = std::fmod((g - b) / d, 6.f) / 6.f;
    else if (mx == g) out.h = ((b - r) / d + 2.f) / 6.f;
    else out.h = ((r - g) / d + 4.f) / 6.f;
    if (out.h < 0.f) out.h += 1.f;
    return out;
}

MusicArtworkStyle styleFromAccent(const nxui::Color& accent, uint64_t signature) {
    MusicArtworkStyle out{};
    out.accent = accent;
    out.spine = {0.030f + accent.r * 0.24f,
                 0.033f + accent.g * 0.24f,
                 0.041f + accent.b * 0.24f, 1.f};
    out.back = {0.016f + accent.r * 0.085f,
                0.018f + accent.g * 0.085f,
                0.024f + accent.b * 0.085f, 1.f};
    out.signature = signature;
    out.sampled = true;
    return out;
}

MusicArtworkStyle sampledArtworkStyle(const DecodedArtwork& image,
                                       const CoverRef& ref) {
    if (image.rgba.empty() || image.width <= 0 || image.height <= 0)
        return {};

    // Hue bins avoid the muddy result of averaging an entire cover. Neutral,
    // near-black and near-white pixels are ignored on the main pass so a real
    // blue/orange/red/etc. visual family wins. The winning bin is then averaged
    // and gently normalised for legible UI accents.
    constexpr int kHueBins = 24;
    struct Bin {
        double weight = 0.0;
        double r = 0.0, g = 0.0, b = 0.0;
    };
    std::array<Bin, kHueBins> bins{};
    double neutralWeight = 0.0;
    double neutralR = 0.0, neutralG = 0.0, neutralB = 0.0;

    const int stepX = std::max(1, image.width / 80);
    const int stepY = std::max(1, image.height / 80);
    for (int y = 0; y < image.height; y += stepY) {
        for (int x = 0; x < image.width; x += stepX) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(image.width) +
                              static_cast<size_t>(x)) * 4u;
            const float a = image.rgba[i + 3] / 255.f;
            if (a < 0.35f) continue;
            const float r = image.rgba[i + 0] / 255.f;
            const float g = image.rgba[i + 1] / 255.f;
            const float b = image.rgba[i + 2] / 255.f;
            const Hsv hsv = rgbToHsv(r, g, b);

            const float neutral = std::clamp(1.f - hsv.s, 0.f, 1.f);
            const float midLight = 1.f - std::min(1.f, std::abs(hsv.v - 0.55f) / 0.55f);
            const double neutralW = static_cast<double>(0.12f + 0.88f * midLight) *
                                    static_cast<double>(0.25f + 0.75f * neutral) * a;
            neutralWeight += neutralW;
            neutralR += neutralW * r;
            neutralG += neutralW * g;
            neutralB += neutralW * b;

            if (hsv.s < 0.16f || hsv.v < 0.13f || hsv.v > 0.94f)
                continue;
            const int binIndex = std::clamp(static_cast<int>(hsv.h * kHueBins),
                                            0, kHueBins - 1);
            const float brightnessWeight = 0.36f + 0.64f * midLight;
            const float saturationWeight = 0.28f + 0.72f * hsv.s;
            const double w = static_cast<double>(brightnessWeight * saturationWeight * a);
            bins[static_cast<size_t>(binIndex)].weight += w;
            bins[static_cast<size_t>(binIndex)].r += w * r;
            bins[static_cast<size_t>(binIndex)].g += w * g;
            bins[static_cast<size_t>(binIndex)].b += w * b;
        }
    }

    const Bin* best = nullptr;
    for (const auto& bin : bins)
        if (!best || bin.weight > best->weight) best = &bin;

    float r = 0.54f, g = 0.72f, b = 1.00f;
    if (best && best->weight > 0.01) {
        r = static_cast<float>(best->r / best->weight);
        g = static_cast<float>(best->g / best->weight);
        b = static_cast<float>(best->b / best->weight);
    } else if (neutralWeight > 0.01) {
        r = static_cast<float>(neutralR / neutralWeight);
        g = static_cast<float>(neutralG / neutralWeight);
        b = static_cast<float>(neutralB / neutralWeight);
    }

    Hsv hsv = rgbToHsv(r, g, b);
    // Keep the album hue but make the accent usable on a black/grey Switch UI.
    hsv.s = std::clamp(std::max(hsv.s, 0.38f), 0.38f, 0.78f);
    hsv.v = std::clamp(hsv.v, 0.62f, 0.84f);
    const nxui::Color accent = hsvToRgb(hsv.h, hsv.s, hsv.v);
    return styleFromAccent(accent, fnv1a64(ref.key()));
}

// Decoder-free fallback used before a cover texture has been requested or for
// an uncommon standalone format that is handled by Texture::loadFromFile.
MusicArtworkStyle safeSyntheticArtworkStyle(const CoverRef& ref) {
    MusicArtworkStyle out{};
    if (!ref.valid()) return out;

    const uint64_t hash = fnv1a64(ref.key());
    const float hue = static_cast<float>(hash & 0xffffu) / 65535.f;
    const float satJitter = static_cast<float>((hash >> 16) & 0xffu) / 255.f;
    const float valJitter = static_cast<float>((hash >> 24) & 0xffu) / 255.f;
    const float saturation = 0.42f + satJitter * 0.20f;
    const float value = 0.70f + valJitter * 0.12f;
    return styleFromAccent(hsvToRgb(hue, saturation, value), hash);
}
} // namespace

bool MusicCoverCache::loadTexture(const CoverRef& ref, nxui::Renderer& ren,
                                  nxui::Texture& out, int maxSide) {
    if (!ref.valid()) return false;

    std::vector<uint8_t> bytes = readCoverBytes(ref);
    int sourceW = 0, sourceH = 0;
    if (!bytes.empty() && preflightArtwork(bytes, sourceW, sourceH)) {
        DecodedArtwork decoded{};
        if (!decodeArtworkOnce(bytes, maxSide, decoded)) {
            DebugLog::log("[music-cover] artwork decode failed path=%s embedded=%d",
                          ref.path.c_str(), ref.embedded ? 1 : 0);
            return false;
        }
        m_styles[ref.key()] = sampledArtworkStyle(decoded, ref);
        return out.loadFromPixels(ren.gpu(), ren, decoded.rgba.data(),
                                  decoded.width, decoded.height);
    }

    if (ref.embedded) {
        DebugLog::log("[music-cover] embedded artwork preflight rejected path=%s offset=%llu size=%llu",
                      ref.path.c_str(),
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
        return false;
    }

    // Preserve V8.3 compatibility for uncommon standalone image formats. They
    // retain the decoder-free synthetic accent because decoding them a second
    // time purely for colour would violate the real-console crash boundary.
    return out.loadFromFile(ren.gpu(), ren, ref.path, maxSide);
}

nxui::Texture* MusicCoverCache::get(const CoverRef& ref, nxui::Renderer& ren, int maxSide) {
    if (!ref.valid()) return nullptr;
    ++m_getEpoch;
    const int qualitySide = maxSide <= 160 ? 160 : 400;
    const std::string key = ref.key() + "@" + std::to_string(qualitySide);
    auto failed = m_failed.find(key);
    if (failed != m_failed.end() && m_getEpoch < failed->second.retryEpoch)
        return nullptr;

    auto found = m_map.find(key);
    if (found != m_map.end()) {
        m_lru.splice(m_lru.begin(), m_lru, found->second);
        return &found->second->texture;
    }

    Entry entry{};
    entry.key = key;
    DebugLog::log("[music-diag] LOAD_COVER begin path=%s embedded=%d bytes=%llu max=%d",
                  ref.path.c_str(), ref.embedded ? 1 : 0,
                  static_cast<unsigned long long>(ref.size), qualitySide);
    if (!loadTexture(ref, ren, entry.texture, qualitySide)) {
        auto& state = m_failed[key];
        state.attempts = std::min<uint32_t>(state.attempts + 1u, 4u);
        // Hardware can transiently fail a texture allocation/descriptor update.
        // Do not blacklist a valid cover for the whole Music session: retry with
        // a slow increasing backoff so a real artwork can replace the fallback.
        state.retryEpoch = m_getEpoch + 900u * static_cast<uint64_t>(state.attempts);
        DebugLog::log("[music-cover] unable to load %s offset=%llu size=%llu retry_attempt=%u",
                      ref.path.c_str(),
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size), state.attempts);
        return nullptr;
    }
    m_failed.erase(key);

    m_lru.push_front(std::move(entry));
    m_map[m_lru.front().key] = m_lru.begin();
    while (m_lru.size() > std::max<size_t>(1, m_maxEntries)) {
        auto last = std::prev(m_lru.end());
        m_map.erase(last->key);
        m_lru.erase(last);
    }
    return &m_lru.front().texture;
}

nxui::Texture* MusicCoverCache::getBack(const CoverRef& ref, nxui::Renderer& ren, int maxSide) {
    // Never perform a second artwork decode/generation for the rear sleeve.
    // MusicPhysicalMediaRenderer falls back to style.back, which remains a
    // smoked album-specific material and requires no extra texture.
    (void)ref;
    (void)ren;
    (void)maxSide;
    return nullptr;
}

MusicCoverCache::~MusicCoverCache() = default;

void MusicCoverCache::requestStyle(const CoverRef& ref) {
    // Kept only for source/API compatibility. V8.3 has no style worker.
    (void)ref;
}

void MusicCoverCache::pollStyleRequest() {
    // No background artwork-analysis job exists in V8.3.
}

MusicArtworkStyle MusicCoverCache::styleFor(const CoverRef& ref) const {
    auto found = m_styles.find(ref.key());
    if (found != m_styles.end()) return found->second;
    return safeSyntheticArtworkStyle(ref);
}

void MusicCoverCache::clear() {
    if (!m_lru.empty())
        DebugLog::log("[music-diag] UNLOAD_COVER clear count=%zu", m_lru.size());
    m_map.clear();
    m_lru.clear();
    m_failed.clear();
    m_styles.clear();
}

} // namespace switchu::menu::music
