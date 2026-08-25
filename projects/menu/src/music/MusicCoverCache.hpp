#pragma once

#include "MusicTypes.hpp"
#include <nxui/core/Texture.hpp>
#include <nxui/core/Types.hpp>

#include <cstddef>
#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

    // Album colour/back-material analysis is intentionally independent from
    // normal texture loading. Only one relevant artwork is processed at once
    // on a worker thread. Results are persisted on SD and validated against a
    // lightweight file signature before reuse on a later boot.
    void requestStyle(const CoverRef& ref);
    void pollStyleRequest();
    MusicArtworkStyle styleFor(const CoverRef& ref) const;

    void clear();
    void resetFailures() { m_failed.clear(); m_styleFailed.clear(); }
    size_t size() const { return m_lru.size(); }

private:
    struct Entry {
        std::string key;
        nxui::Texture texture;
    };
    struct StyleResult {
        std::string key;
        uint64_t signature = 0;
        MusicArtworkStyle style{};
    };
    struct StyleWorkerState {
        std::atomic<bool> cancelRequested{false};
        std::atomic<bool> ready{false};
        std::mutex resultMutex;
        std::optional<StyleResult> result;
    };
    using List = std::list<Entry>;
    using Map = std::unordered_map<std::string, List::iterator>;

    bool loadTexture(const CoverRef& ref, nxui::Renderer& ren, nxui::Texture& out, int maxSide);
    static MusicArtworkStyle sampleArtworkStyle(const CoverRef& ref, uint64_t signature,
                                                const std::atomic<bool>* cancelRequested);
    static uint64_t coverSignature(const CoverRef& ref);

    void ensurePersistentStyleCacheLoaded();
    void persistStyleCache();
    void prunePersistentStyleCache();
    void touchStyle(const std::string& key, MusicArtworkStyle& style);

    size_t m_maxEntries = 18;
    List m_lru;
    Map m_map;
    std::unordered_set<std::string> m_failed;

    std::unordered_map<std::string, MusicArtworkStyle> m_styles;
    std::unordered_map<std::string, MusicArtworkStyle> m_persistedStyles;
    std::unordered_set<std::string> m_styleFailed;
    std::shared_ptr<StyleWorkerState> m_styleWorker;
    bool m_styleRunning = false;
    bool m_persistentStylesLoaded = false;
    bool m_persistentCacheDirty = false;
    uint64_t m_lastPersistEpochSec = 0;
    std::string m_stylePendingKey;
};

} // namespace switchu::menu::music
