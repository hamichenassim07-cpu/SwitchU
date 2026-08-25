#pragma once

#include "MusicTypes.hpp"
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nxui { class Renderer; }

namespace switchu::menu::music {

struct MusicArtworkStyle {
    nxui::Color accent{0.54f, 0.72f, 1.00f, 1.f};
    nxui::Color spine{0.10f, 0.12f, 0.16f, 1.f};
    nxui::Color back{0.035f, 0.040f, 0.052f, 1.f};
    std::string backTexturePath;
    uint64_t signature = 0;
    uint64_t lastUsedEpochSec = 0;
    bool sampled = false;
};

class MusicCoverCache {
public:
    explicit MusicCoverCache(size_t maxEntries = 18) : m_maxEntries(maxEntries) {}
    ~MusicCoverCache();

    nxui::Texture* get(const CoverRef& ref, nxui::Renderer& ren, int maxSide = 256);
    nxui::Texture* getBack(const CoverRef& ref, nxui::Renderer& ren, int maxSide = 160);

    // V8.3 safe-style compatibility API. Per-album colours are resolved without
    // re-decoding artwork or launching a background image worker.
    void requestStyle(const CoverRef& ref);
    void pollStyleRequest();
    MusicArtworkStyle styleFor(const CoverRef& ref) const;

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
