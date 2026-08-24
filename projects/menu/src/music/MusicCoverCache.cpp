#include "MusicCoverCache.hpp"

#include "core/DebugLog.hpp"
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <fstream>
#include <vector>

namespace switchu::menu::music {

namespace {
constexpr uint64_t kMaxEmbeddedCoverBytes = 6ULL * 1024ULL * 1024ULL;
}

bool MusicCoverCache::loadTexture(const CoverRef& ref, nxui::Renderer& ren,
                                  nxui::Texture& out, int maxSide) {
    if (!ref.valid()) return false;

    if (!ref.embedded)
        return out.loadFromFile(ren.gpu(), ren, ref.path, maxSide);

    if (ref.size == 0 || ref.size > kMaxEmbeddedCoverBytes)
        return false;

    std::ifstream file(ref.path, std::ios::binary);
    if (!file.is_open()) return false;
    file.seekg(static_cast<std::streamoff>(ref.offset), std::ios::beg);
    if (!file) return false;

    std::vector<uint8_t> bytes(static_cast<size_t>(ref.size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
        return false;

    return out.loadFromMemory(ren.gpu(), ren, bytes.data(), bytes.size(), maxSide);
}

const nxui::Texture* MusicCoverCache::get(const CoverRef& ref, nxui::Renderer& ren,
                                          int maxSide) {
    if (!ref.valid()) return nullptr;
    // V0.02 uses two quality buckets instead of generating a distinct GPU
    // texture for every requested draw size. In V0.01 the same artwork could
    // exist simultaneously at 96/128/160/256/384/512px, creating avoidable
    // GPU pressure and making navigation-related crashes much more likely.
    const int qualitySide = maxSide <= 160 ? 160 : 400;
    const std::string key = ref.key() + "@" + std::to_string(qualitySide);
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
        return nullptr;
    }

    m_lru.push_front(std::move(entry));
    m_map[m_lru.front().key] = m_lru.begin();
    DebugLog::log("[music-diag] LOAD_COVER done key=%s cache=%zu",
                  m_lru.front().key.c_str(), m_lru.size());

    while (m_lru.size() > std::max<size_t>(1, m_maxEntries)) {
        auto last = std::prev(m_lru.end());
        DebugLog::log("[music-diag] UNLOAD_COVER evict key=%s", last->key.c_str());
        m_map.erase(last->key);
        m_lru.erase(last);
    }
    return &m_lru.front().texture;
}

void MusicCoverCache::clear() {
    if (!m_lru.empty())
        DebugLog::log("[music-diag] UNLOAD_COVER clear count=%zu", m_lru.size());
    m_map.clear();
    m_lru.clear();
}

} // namespace switchu::menu::music
