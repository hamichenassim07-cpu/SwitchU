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
    const std::string key = ref.key() + "@" + std::to_string(maxSide);
    auto found = m_map.find(key);
    if (found != m_map.end()) {
        m_lru.splice(m_lru.begin(), m_lru, found->second);
        return &found->second->texture;
    }

    Entry entry{};
    entry.key = key;
    if (!loadTexture(ref, ren, entry.texture, maxSide)) {
        DebugLog::log("[music-cover] unable to load %s offset=%llu size=%llu",
                      ref.path.c_str(),
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
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

void MusicCoverCache::clear() {
    m_map.clear();
    m_lru.clear();
}

} // namespace switchu::menu::music
