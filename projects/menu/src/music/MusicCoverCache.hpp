#pragma once

#include "MusicTypes.hpp"
#include <nxui/core/Texture.hpp>

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nxui { class Renderer; }

namespace switchu::menu::music {

class MusicCoverCache {
public:
    explicit MusicCoverCache(size_t maxEntries = 18) : m_maxEntries(maxEntries) {}

    const nxui::Texture* get(const CoverRef& ref, nxui::Renderer& ren, int maxSide = 256);
    void clear();
    void resetFailures() { m_failed.clear(); }
    size_t size() const { return m_lru.size(); }

private:
    struct Entry {
        std::string key;
        nxui::Texture texture;
    };
    using List = std::list<Entry>;
    using Map = std::unordered_map<std::string, List::iterator>;

    bool loadTexture(const CoverRef& ref, nxui::Renderer& ren, nxui::Texture& out, int maxSide);

    size_t m_maxEntries = 18;
    List m_lru;
    Map m_map;
    std::unordered_set<std::string> m_failed;
};

} // namespace switchu::menu::music
