#pragma once
#include <string>
#include <unordered_set>

namespace switchu::menu::music {
// Independent UI preferences. Never writes the library, playlists or decoder.
class MusicPreferences {
public:
    explicit MusicPreferences(std::string path = "sdmc:/config/SwitchU/music/preferences-v1.txt")
        : m_path(std::move(path)) {}
    bool load();
    bool save();
    bool favourite(const std::string& albumKey) const { return m_favourites.count(albumKey) != 0; }
    bool toggle(const std::string& albumKey);
    void remember(const std::string& key) { if (key != m_lastAlbum) { m_lastAlbum = key; m_dirty = true; } }
    const std::string& lastAlbum() const { return m_lastAlbum; }
    size_t favouriteCount() const { return m_favourites.size(); }
private:
    std::string m_path, m_lastAlbum;
    std::unordered_set<std::string> m_favourites;
    bool m_dirty = false;
};
}
