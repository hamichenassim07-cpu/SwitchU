#include "MusicCoverCache.hpp"

#include "core/DebugLog.hpp"
#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>

#include <algorithm>
#include <cmath>
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

// V8.3 console crash fix.
//
// The V8.1 console log proves the library scan completes, normal cover textures
// load successfully, and the process dies immediately after the secondary
// "[music-style] worker begin" line for Kiss Land / Odd Look.  The unsafe part
// was therefore the second image decode performed by the detached style worker,
// not the HOME carousel, library scan or FIX6 audio engine.
//
// Resolve an album-specific visual style without touching image bytes, the GPU,
// the filesystem or another thread.  This keeps the physical sleeve/vinyl DA
// alive while removing the crash path completely.
MusicArtworkStyle safeSyntheticArtworkStyle(const CoverRef& ref) {
    MusicArtworkStyle out{};
    if (!ref.valid()) return out;

    const uint64_t hash = fnv1a64(ref.key());
    const float hue = static_cast<float>(hash & 0xffffu) / 65535.f;
    const float satJitter = static_cast<float>((hash >> 16) & 0xffu) / 255.f;
    const float valJitter = static_cast<float>((hash >> 24) & 0xffu) / 255.f;
    const float saturation = 0.42f + satJitter * 0.20f;
    const float value = 0.70f + valJitter * 0.12f;

    out.accent = hsvToRgb(hue, saturation, value);
    out.spine = {0.030f + out.accent.r * 0.24f,
                 0.033f + out.accent.g * 0.24f,
                 0.041f + out.accent.b * 0.24f, 1.f};
    out.back = {0.016f + out.accent.r * 0.085f,
                0.018f + out.accent.g * 0.085f,
                0.024f + out.accent.b * 0.085f, 1.f};
    out.signature = hash;
    // Existing renderer code uses 'sampled' as "resolved style available".
    out.sampled = true;
    return out;
}
} // namespace

bool MusicCoverCache::loadTexture(const CoverRef& ref, nxui::Renderer& ren,
                                  nxui::Texture& out, int maxSide) {
    if (!ref.valid()) return false;
    if (!ref.embedded)
        return out.loadFromFile(ren.gpu(), ren, ref.path, maxSide);

    // Embedded APIC validation stays on the render/UI thread.  This exact path
    // completed successfully in the crash session before the old style worker
    // was started, so retain it while rejecting malformed offsets/sizes.
    std::vector<uint8_t> bytes = readCoverBytes(ref);
    if (bytes.empty()) return false;
    int w = 0, h = 0;
    if (!preflightArtwork(bytes, w, h)) {
        DebugLog::log("[music-cover] embedded artwork preflight rejected path=%s offset=%llu size=%llu",
                      ref.path.c_str(),
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
        return false;
    }
    return out.loadFromMemory(ren.gpu(), ren, bytes.data(), bytes.size(), maxSide);
}

nxui::Texture* MusicCoverCache::get(const CoverRef& ref, nxui::Renderer& ren, int maxSide) {
    if (!ref.valid()) return nullptr;
    const int qualitySide = maxSide <= 160 ? 160 : 400;
    const std::string key = ref.key() + "@" + std::to_string(qualitySide);
    if (m_failed.find(key) != m_failed.end()) return nullptr;

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
        DebugLog::log("[music-cover] unable to load %s offset=%llu size=%llu",
                      ref.path.c_str(),
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
        m_failed.insert(key);
        return nullptr;
    }

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
    return safeSyntheticArtworkStyle(ref);
}

void MusicCoverCache::clear() {
    if (!m_lru.empty())
        DebugLog::log("[music-diag] UNLOAD_COVER clear count=%zu", m_lru.size());
    m_map.clear();
    m_lru.clear();
    m_failed.clear();
}

} // namespace switchu::menu::music
