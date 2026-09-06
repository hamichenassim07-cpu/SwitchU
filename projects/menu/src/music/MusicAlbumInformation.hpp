#pragma once

#include "MusicTypes.hpp"
#include <algorithm>
#include <limits>

namespace switchu::menu::music::albuminfo {

// Display-only sanitisation: the library and audio metadata are never changed.
inline std::string singleLine(const std::string& source, const char* fallback) {
    std::string line;
    line.reserve(std::min<size_t>(source.size(), 4096));
    for (size_t i = 0; i < source.size() && line.size() < 4096;) {
        const auto c = static_cast<unsigned char>(source[i]);
        if (c < 0x20 || c == 0x7f) {
            if (!line.empty() && line.back() != ' ') line += ' ';
            ++i;
            continue;
        }
        size_t len = c < 0x80 ? 1 : (c >= 0xc2 && c <= 0xdf ? 2 :
                     (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 0)));
        bool valid = len && i + len <= source.size();
        for (size_t j = 1; valid && j < len; ++j)
            valid = (static_cast<unsigned char>(source[i+j]) & 0xc0) == 0x80;
        if (valid && len >= 3) {
            const auto next = static_cast<unsigned char>(source[i+1]);
            valid = !(c == 0xe0 && next < 0xa0) && !(c == 0xed && next >= 0xa0) &&
                    !(c == 0xf0 && next < 0x90) && !(c == 0xf4 && next >= 0x90);
        }
        if (!valid) {
            if (line.size() + 3 > 4096) break;
            line += "�"; ++i; continue;
        }
        if (line.size() + len > 4096) break;
        // Unicode line/paragraph separators also stay on a single display line.
        if (len == 3 && c == 0xe2 && static_cast<unsigned char>(source[i+1]) == 0x80 &&
            (source[i+2] == '\xa8' || source[i+2] == '\xa9')) line += ' ';
        else line.append(source, i, len);
        i += len;
    }
    const auto first = line.find_first_not_of(' ');
    if (first == std::string::npos) return fallback;
    return line.substr(first, line.find_last_not_of(' ') - first + 1);
}

inline std::string duration(uint64_t ms) {
    if (ms == 0) return "0 min";
    const uint64_t minutes = ms / 60000;
    if (minutes == 0) return "< 1 min";
    if (minutes < 60) return std::to_string(minutes) + " min";
    const uint64_t remainder = minutes % 60;
    return std::to_string(minutes / 60) + " h " +
           (remainder < 10 ? "0" : "") + std::to_string(remainder);
}

struct Summary { std::string duration, count; };
inline Summary summarise(const Album& album, const LibrarySnapshot& library) {
    uint64_t total = 0;
    size_t count = 0, unknown = 0;
    for (size_t index : album.tracks) {
        if (index >= library.tracks.size()) continue;
        ++count;
        const uint64_t ms = library.tracks[index].durationMs;
        if (ms == 0) ++unknown;
        total += std::min(ms, std::numeric_limits<uint64_t>::max() - total);
    }
    std::string time = duration(total);
    if (unknown) time = total >= 60000 ? "≥ " + time : "Durée inconnue";
    return {time, std::to_string(count) + (count == 1 ? " piste" : " pistes")};
}
} // namespace switchu::menu::music::albuminfo
