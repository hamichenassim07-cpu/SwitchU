#include "MusicPreferences.hpp"
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <unistd.h>

namespace switchu::menu::music {
bool MusicPreferences::load() {
    std::ifstream file(m_path);
    if (!file) return true;
    try {
        std::error_code sizeError;
        if (std::filesystem::file_size(m_path, sizeError) > 512u * 1024u || sizeError) return false;
        std::string magic;
        std::getline(file, magic);
        if (magic != "SWITCHU_MUSIC_UI 1") return false;
        std::unordered_set<std::string> favourites;
        std::string last, key;
        char kind = 0;
        while (file >> kind) {
            if (!(file >> std::quoted(key)) || key.size() > 4096) return false;
            if (kind == 'L') last = key;
            else if (kind == 'F' && !key.empty() && favourites.size() < 8192) favourites.insert(key);
            else return false;
        }
        if (!file.eof()) return false;
        m_favourites = std::move(favourites);
        m_lastAlbum = std::move(last);
        m_dirty = false;
        return true;
    } catch (...) { return false; }
}
bool MusicPreferences::save() {
    if (!m_dirty) return true;
    try {
        std::ostringstream encoded;
        encoded << "SWITCHU_MUSIC_UI 1\nL " << std::quoted(m_lastAlbum) << '\n';
        for (const auto& key : m_favourites) encoded << "F " << std::quoted(key) << '\n';
        const auto data = encoded.str();
        if (data.size() > 512u * 1024u) return false;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(m_path).parent_path(), ec);
        if (ec) return false;
        const auto temporary = m_path + ".tmp";
        FILE* f = std::fopen(temporary.c_str(), "wb");
        if (!f) return false;
        bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        if (std::fflush(f) != 0) ok = false;
        if (ok && ::fsync(::fileno(f)) != 0) ok = false;
        if (std::fclose(f) != 0) ok = false;
        // Atomic replacement; do not remove the previous preferences first.
        if (ok) ok = std::rename(temporary.c_str(), m_path.c_str()) == 0;
        if (!ok) { std::remove(temporary.c_str()); return false; }
        m_dirty = false;
        return true;
    } catch (...) { return false; }
}
bool MusicPreferences::toggle(const std::string& key) {
    if (key.empty()) return false;
    const bool had = favourite(key);
    if (had) m_favourites.erase(key); else m_favourites.insert(key);
    m_dirty = true;
    if (save()) return true;
    // Keep the visual status honest when the SD cannot persist the operation.
    if (had) m_favourites.insert(key); else m_favourites.erase(key);
    m_dirty = true;
    return false;
}
}
