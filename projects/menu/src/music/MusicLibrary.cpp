#include "MusicLibrary.hpp"

#include "core/DebugLog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace switchu::menu::music {
namespace {

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint32_t syncsafe32(const uint8_t* p) {
    return (uint32_t(p[0] & 0x7F) << 21) | (uint32_t(p[1] & 0x7F) << 14) |
           (uint32_t(p[2] & 0x7F) << 7) | uint32_t(p[3] & 0x7F);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c) && c != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string latin1ToUtf8(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        const uint8_t c = data[i];
        if (c == 0) break;
        appendUtf8(out, c);
    }
    return trim(out);
}

std::string utf16ToUtf8(const uint8_t* data, size_t size, bool bigEndianDefault) {
    if (size < 2) return {};
    bool be = bigEndianDefault;
    size_t pos = 0;
    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        be = true; pos = 2;
    } else if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        be = false; pos = 2;
    }

    std::string out;
    while (pos + 1 < size) {
        uint16_t w = be ? (uint16_t(data[pos]) << 8 | data[pos + 1])
                        : (uint16_t(data[pos + 1]) << 8 | data[pos]);
        pos += 2;
        if (w == 0) break;
        uint32_t cp = w;
        if (w >= 0xD800 && w <= 0xDBFF && pos + 1 < size) {
            uint16_t lo = be ? (uint16_t(data[pos]) << 8 | data[pos + 1])
                             : (uint16_t(data[pos + 1]) << 8 | data[pos]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                pos += 2;
                cp = 0x10000 + ((uint32_t(w - 0xD800) << 10) | (lo - 0xDC00));
            }
        }
        appendUtf8(out, cp);
    }
    return trim(out);
}

std::string decodeId3Text(const uint8_t* data, size_t size) {
    if (!data || size == 0) return {};
    const uint8_t encoding = data[0];
    const uint8_t* text = data + 1;
    const size_t textSize = size - 1;
    switch (encoding) {
        case 0: return latin1ToUtf8(text, textSize);
        case 1: return utf16ToUtf8(text, textSize, false);
        case 2: return utf16ToUtf8(text, textSize, true);
        case 3: {
            std::string value(reinterpret_cast<const char*>(text), textSize);
            const auto nul = value.find('\0');
            if (nul != std::string::npos) value.resize(nul);
            return trim(value);
        }
        default: return {};
    }
}

size_t skipEncodedTerminator(const uint8_t* data, size_t size, size_t pos, uint8_t encoding) {
    if (encoding == 1 || encoding == 2) {
        while (pos + 1 < size) {
            if (data[pos] == 0 && data[pos + 1] == 0)
                return pos + 2;
            pos += 2;
        }
        return size;
    }
    while (pos < size && data[pos] != 0) ++pos;
    return std::min(size, pos + 1);
}

int parseLeadingInt(const std::string& value) {
    size_t i = 0;
    while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
    int result = 0;
    bool any = false;
    while (i < value.size() && std::isdigit(static_cast<unsigned char>(value[i]))) {
        any = true;
        result = result * 10 + (value[i] - '0');
        ++i;
    }
    return any ? result : 0;
}

CoverRef findExternalCover(const std::filesystem::path& audioPath) {
    static constexpr const char* names[] = {
        "cover.jpg", "cover.jpeg", "cover.png",
        "folder.jpg", "folder.jpeg", "folder.png",
        "Cover.jpg", "Folder.jpg"
    };
    std::error_code ec;
    const auto dir = audioPath.parent_path();
    for (const char* name : names) {
        const auto candidate = dir / name;
        if (std::filesystem::is_regular_file(candidate, ec))
            return {candidate.string(), 0, 0, false};
        ec.clear();
    }
    return {};
}

int64_t modifiedStamp(const std::filesystem::path& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<int64_t>(t.time_since_epoch().count());
}

struct Mp3FrameInfo {
    int bitrateKbps = 0;
    int sampleRate = 0;
    int samplesPerFrame = 0;
    int channels = 2;
    int version = 1;
    int layer = 3;
    size_t frameSize = 0;
};

bool decodeMp3Header(uint32_t h, Mp3FrameInfo& out) {
    if ((h & 0xFFE00000u) != 0xFFE00000u) return false;
    const int versionBits = (h >> 19) & 3;
    const int layerBits = (h >> 17) & 3;
    const int bitrateIdx = (h >> 12) & 0xF;
    const int sampleIdx = (h >> 10) & 3;
    const int padding = (h >> 9) & 1;
    const int channelMode = (h >> 6) & 3;
    if (versionBits == 1 || layerBits == 0 || bitrateIdx == 0 || bitrateIdx == 15 || sampleIdx == 3)
        return false;

    const int version = versionBits == 3 ? 1 : (versionBits == 2 ? 2 : 25);
    const int layer = 4 - layerBits;
    static constexpr int srBase[] = {44100, 48000, 32000};
    int sr = srBase[sampleIdx];
    if (version == 2) sr /= 2;
    if (version == 25) sr /= 4;

    static constexpr int brV1L1[16] = {0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0};
    static constexpr int brV1L2[16] = {0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0};
    static constexpr int brV1L3[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static constexpr int brV2L1[16] = {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0};
    static constexpr int brV2L23[16] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};

    int br = 0;
    if (version == 1) {
        br = layer == 1 ? brV1L1[bitrateIdx] : (layer == 2 ? brV1L2[bitrateIdx] : brV1L3[bitrateIdx]);
    } else {
        br = layer == 1 ? brV2L1[bitrateIdx] : brV2L23[bitrateIdx];
    }
    if (br <= 0 || sr <= 0) return false;

    size_t frameSize = 0;
    int samples = 0;
    if (layer == 1) {
        frameSize = static_cast<size_t>((12 * br * 1000 / sr + padding) * 4);
        samples = 384;
    } else if (layer == 2) {
        frameSize = static_cast<size_t>(144 * br * 1000 / sr + padding);
        samples = 1152;
    } else {
        const int coeff = version == 1 ? 144 : 72;
        frameSize = static_cast<size_t>(coeff * br * 1000 / sr + padding);
        samples = version == 1 ? 1152 : 576;
    }

    out.bitrateKbps = br;
    out.sampleRate = sr;
    out.samplesPerFrame = samples;
    out.channels = channelMode == 3 ? 1 : 2;
    out.version = version;
    out.layer = layer;
    out.frameSize = frameSize;
    return frameSize >= 4;
}

uint64_t estimateMp3Duration(std::ifstream& file, uint64_t fileSize, uint64_t audioStart) {
    if (fileSize <= audioStart + 4) return 0;
    file.clear();
    file.seekg(static_cast<std::streamoff>(audioStart), std::ios::beg);

    std::array<uint8_t, 8192> buf{};
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    const size_t got = static_cast<size_t>(file.gcount());
    for (size_t i = 0; i + 4 <= got; ++i) {
        const uint32_t h = be32(buf.data() + i);
        Mp3FrameInfo info{};
        if (!decodeMp3Header(h, info)) continue;

        // Xing/Info frame count gives a reliable VBR duration when present.
        if (info.layer == 3) {
            const size_t sideInfo = info.version == 1
                ? (info.channels == 1 ? 17u : 32u)
                : (info.channels == 1 ? 9u : 17u);
            const size_t xing = i + 4 + sideInfo;
            if (xing + 12 <= got &&
                (std::memcmp(buf.data() + xing, "Xing", 4) == 0 ||
                 std::memcmp(buf.data() + xing, "Info", 4) == 0)) {
                const uint32_t flags = be32(buf.data() + xing + 4);
                if ((flags & 0x1u) != 0 && xing + 12 <= got) {
                    const uint32_t frames = be32(buf.data() + xing + 8);
                    if (frames > 0) {
                        const double seconds =
                            double(frames) * double(info.samplesPerFrame) / double(info.sampleRate);
                        return static_cast<uint64_t>(seconds * 1000.0);
                    }
                }
            }
        }

        const uint64_t bytes = fileSize > audioStart + i ? fileSize - (audioStart + i) : 0;
        if (bytes > 0 && info.bitrateKbps > 0) {
            return static_cast<uint64_t>((double(bytes) * 8.0 * 1000.0) /
                                         (double(info.bitrateKbps) * 1000.0));
        }
        break;
    }
    return 0;
}

bool parseMp3(const std::filesystem::path& path, Track& track) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    file.seekg(0, std::ios::end);
    const uint64_t fileSize = static_cast<uint64_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    uint64_t audioStart = 0;
    std::array<uint8_t, 10> hdr{};
    if (file.read(reinterpret_cast<char*>(hdr.data()), hdr.size()) &&
        std::memcmp(hdr.data(), "ID3", 3) == 0) {
        const int version = hdr[3];
        const uint32_t tagSize = syncsafe32(hdr.data() + 6);
        audioStart = 10ULL + tagSize;
        if (audioStart <= fileSize && tagSize <= 64u * 1024u * 1024u) {
            std::vector<uint8_t> tag(tagSize);
            file.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
            size_t pos = 0;
            while (pos + 10 <= tag.size()) {
                const char idChars[5] = {
                    char(tag[pos]), char(tag[pos+1]), char(tag[pos+2]), char(tag[pos+3]), 0
                };
                if (idChars[0] == 0) break;
                const std::string id(idChars);
                uint32_t frameSize = version >= 4
                    ? syncsafe32(tag.data() + pos + 4)
                    : be32(tag.data() + pos + 4);
                if (frameSize == 0 || pos + 10ULL + frameSize > tag.size()) break;
                const uint8_t* data = tag.data() + pos + 10;

                if (id == "TIT2") track.title = decodeId3Text(data, frameSize);
                else if (id == "TPE1") track.artist = decodeId3Text(data, frameSize);
                else if (id == "TALB") track.album = decodeId3Text(data, frameSize);
                else if (id == "TRCK") track.trackNumber = parseLeadingInt(decodeId3Text(data, frameSize));
                else if (id == "TYER" || id == "TDRC") track.year = parseLeadingInt(decodeId3Text(data, frameSize));
                else if (id == "APIC" && frameSize > 4 && !track.cover.valid()) {
                    const uint8_t encoding = data[0];
                    size_t p = 1;
                    while (p < frameSize && data[p] != 0) ++p; // MIME
                    if (p < frameSize) ++p;
                    if (p < frameSize) ++p; // picture type
                    p = skipEncodedTerminator(data, frameSize, p, encoding);
                    if (p < frameSize) {
                        track.cover.path = path.string();
                        track.cover.offset = 10ULL + pos + 10ULL + p;
                        track.cover.size = frameSize - p;
                        track.cover.embedded = true;
                    }
                }
                pos += 10ULL + frameSize;
            }
        }
    }

    // ID3v1 fallback for older files.
    if ((track.title.empty() || track.artist.empty() || track.album.empty()) && fileSize >= 128) {
        std::array<uint8_t, 128> id3v1{};
        file.clear();
        file.seekg(static_cast<std::streamoff>(fileSize - 128), std::ios::beg);
        if (file.read(reinterpret_cast<char*>(id3v1.data()), id3v1.size()) &&
            std::memcmp(id3v1.data(), "TAG", 3) == 0) {
            if (track.title.empty()) track.title = latin1ToUtf8(id3v1.data() + 3, 30);
            if (track.artist.empty()) track.artist = latin1ToUtf8(id3v1.data() + 33, 30);
            if (track.album.empty()) track.album = latin1ToUtf8(id3v1.data() + 63, 30);
            if (track.year == 0) track.year = parseLeadingInt(latin1ToUtf8(id3v1.data() + 93, 4));
        }
    }

    track.durationMs = estimateMp3Duration(file, fileSize, audioStart);
    return true;
}

bool parseFlac(const std::filesystem::path& path, Track& track) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::array<uint8_t, 4> sig{};
    if (!file.read(reinterpret_cast<char*>(sig.data()), sig.size()) ||
        std::memcmp(sig.data(), "fLaC", 4) != 0)
        return false;

    bool last = false;
    while (!last && file) {
        std::array<uint8_t, 4> mh{};
        if (!file.read(reinterpret_cast<char*>(mh.data()), mh.size())) break;
        last = (mh[0] & 0x80) != 0;
        const uint8_t type = mh[0] & 0x7F;
        const uint32_t len = (uint32_t(mh[1]) << 16) | (uint32_t(mh[2]) << 8) | mh[3];
        const uint64_t blockDataOffset = static_cast<uint64_t>(file.tellg());
        if (len > 32u * 1024u * 1024u) return false;
        std::vector<uint8_t> block(len);
        if (len > 0 && !file.read(reinterpret_cast<char*>(block.data()), len)) break;

        if (type == 0 && len >= 18) { // STREAMINFO
            const uint8_t* p = block.data() + 10;
            const uint32_t sampleRate = (uint32_t(p[0]) << 12) |
                                        (uint32_t(p[1]) << 4) |
                                        (uint32_t(p[2]) >> 4);
            const uint64_t totalSamples =
                (uint64_t(p[3] & 0x0F) << 32) |
                (uint64_t(p[4]) << 24) |
                (uint64_t(p[5]) << 16) |
                (uint64_t(p[6]) << 8) |
                uint64_t(p[7]);
            if (sampleRate > 0 && totalSamples > 0)
                track.durationMs = totalSamples * 1000ULL / sampleRate;
        } else if (type == 4 && len >= 8) { // VORBIS_COMMENT
            size_t p = 0;
            if (p + 4 > block.size()) continue;
            const uint32_t vendorLen = le32(block.data() + p); p += 4;
            if (p + vendorLen + 4 > block.size()) continue;
            p += vendorLen;
            const uint32_t count = le32(block.data() + p); p += 4;
            for (uint32_t i = 0; i < count && p + 4 <= block.size(); ++i) {
                const uint32_t slen = le32(block.data() + p); p += 4;
                if (p + slen > block.size()) break;
                std::string kv(reinterpret_cast<const char*>(block.data() + p), slen);
                p += slen;
                const auto eq = kv.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = lower(kv.substr(0, eq));
                const std::string value = trim(kv.substr(eq + 1));
                if (key == "title") track.title = value;
                else if (key == "artist" || key == "albumartist") {
                    if (track.artist.empty() || key == "artist") track.artist = value;
                }
                else if (key == "album") track.album = value;
                else if (key == "tracknumber") track.trackNumber = parseLeadingInt(value);
                else if (key == "date" || key == "year") track.year = parseLeadingInt(value);
            }
        } else if (type == 6 && len >= 32 && !track.cover.valid()) { // PICTURE
            size_t p = 0;
            if (p + 8 > block.size()) continue;
            p += 4; // picture type
            const uint32_t mimeLen = be32(block.data() + p); p += 4;
            if (p + mimeLen + 4 > block.size()) continue;
            p += mimeLen;
            const uint32_t descLen = be32(block.data() + p); p += 4;
            if (p + descLen + 20 > block.size()) continue;
            p += descLen;
            p += 16; // width height depth colours
            const uint32_t dataLen = be32(block.data() + p); p += 4;
            if (dataLen > 0 && p + dataLen <= block.size()) {
                track.cover.path = path.string();
                track.cover.offset = blockDataOffset + p;
                track.cover.size = dataLen;
                track.cover.embedded = true;
            }
        }
    }
    return true;
}

bool parseWav(const std::filesystem::path& path, Track& track) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::array<uint8_t, 12> hdr{};
    if (!file.read(reinterpret_cast<char*>(hdr.data()), hdr.size()) ||
        std::memcmp(hdr.data(), "RIFF", 4) != 0 ||
        std::memcmp(hdr.data() + 8, "WAVE", 4) != 0)
        return false;

    uint32_t byteRate = 0;
    uint32_t dataSize = 0;
    while (file) {
        std::array<uint8_t, 8> ch{};
        if (!file.read(reinterpret_cast<char*>(ch.data()), ch.size())) break;
        const uint32_t size = le32(ch.data() + 4);
        const std::string id(reinterpret_cast<const char*>(ch.data()), 4);
        if (id == "fmt " && size >= 16) {
            std::vector<uint8_t> fmt(std::min<uint32_t>(size, 64));
            if (!file.read(reinterpret_cast<char*>(fmt.data()), fmt.size())) break;
            if (fmt.size() >= 12) byteRate = le32(fmt.data() + 8);
            if (size > fmt.size()) file.seekg(size - fmt.size(), std::ios::cur);
        } else if (id == "data") {
            dataSize = size;
            file.seekg(size, std::ios::cur);
        } else {
            file.seekg(size, std::ios::cur);
        }
        if ((size & 1u) != 0) file.seekg(1, std::ios::cur);
    }
    if (byteRate > 0 && dataSize > 0)
        track.durationMs = uint64_t(dataSize) * 1000ULL / byteRate;
    return true;
}

bool supportedExtension(const std::filesystem::path& p) {
    const std::string ext = lower(p.extension().string());
    return ext == ".mp3" || ext == ".flac" || ext == ".wav";
}

Track parseTrack(const std::filesystem::path& path) {
    Track track{};
    track.path = path.string();
    track.id = switchu::music::fnv1a64(track.path.data(), track.path.size());
    track.modifiedStamp = modifiedStamp(path);

    const std::string ext = lower(path.extension().string());
    if (ext == ".mp3") parseMp3(path, track);
    else if (ext == ".flac") parseFlac(path, track);
    else if (ext == ".wav") parseWav(path, track);

    if (track.title.empty()) track.title = path.stem().string();
    if (track.artist.empty()) track.artist = "Artiste inconnu";
    if (track.album.empty()) track.album = "Sans album";
    if (!track.cover.valid()) track.cover = findExternalCover(path);
    return track;
}

std::string normalizedKey(const std::string& value) {
    std::string out = lower(trim(value));
    return out;
}

} // namespace

LibrarySnapshot MusicLibrary::scan(const std::string& root,
                                   std::atomic<uint32_t>* filesVisited,
                                   std::atomic<uint32_t>* tracksFound) {
    LibrarySnapshot result{};
    result.scanGeneration = static_cast<uint64_t>(std::time(nullptr));

    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        std::filesystem::create_directories(root, ec);
        DebugLog::log("[music-library] created %s ec=%d", root.c_str(), ec.value());
    }

    std::vector<std::filesystem::path> paths;
    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec);
    std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        if (filesVisited) ++(*filesVisited);
        std::error_code typeEc;
        if (!it->is_regular_file(typeEc)) continue;
        if (!supportedExtension(it->path())) continue;
        paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());

    result.tracks.reserve(paths.size());
    for (const auto& path : paths) {
        Track track = parseTrack(path);
        result.trackById[track.id] = result.tracks.size();
        result.trackByPath[track.path] = result.tracks.size();
        result.tracks.push_back(std::move(track));
        if (tracksFound) ++(*tracksFound);
    }

    std::map<std::string, size_t> albumMap;
    for (size_t ti = 0; ti < result.tracks.size(); ++ti) {
        const auto& track = result.tracks[ti];
        const std::string key = normalizedKey(track.artist) + "\n" + normalizedKey(track.album);
        auto found = albumMap.find(key);
        size_t ai = 0;
        if (found == albumMap.end()) {
            ai = result.albums.size();
            albumMap[key] = ai;
            Album album{};
            album.key = key;
            album.title = track.album;
            album.artist = track.artist;
            album.year = track.year;
            album.cover = track.cover;
            album.newestStamp = track.modifiedStamp;
            result.albums.push_back(std::move(album));
        } else {
            ai = found->second;
        }
        auto& album = result.albums[ai];
        album.tracks.push_back(ti);
        album.newestStamp = std::max(album.newestStamp, track.modifiedStamp);
        if (!album.cover.valid() && track.cover.valid()) album.cover = track.cover;
        if (album.year == 0 && track.year != 0) album.year = track.year;
    }

    for (auto& album : result.albums) {
        std::stable_sort(album.tracks.begin(), album.tracks.end(), [&](size_t a, size_t b) {
            const auto& ta = result.tracks[a];
            const auto& tb = result.tracks[b];
            if (ta.trackNumber > 0 && tb.trackNumber > 0 && ta.trackNumber != tb.trackNumber)
                return ta.trackNumber < tb.trackNumber;
            if ((ta.trackNumber > 0) != (tb.trackNumber > 0))
                return ta.trackNumber > 0;
            return lower(ta.title) < lower(tb.title);
        });
    }

    std::sort(result.albums.begin(), result.albums.end(), [](const Album& a, const Album& b) {
        if (lower(a.artist) != lower(b.artist)) return lower(a.artist) < lower(b.artist);
        return lower(a.title) < lower(b.title);
    });

    // Rebuild album references after sorting.
    std::unordered_map<std::string, size_t> albumByKey;
    for (size_t i = 0; i < result.albums.size(); ++i)
        albumByKey[result.albums[i].key] = i;

    std::map<std::string, size_t> artistMap;
    for (size_t ti = 0; ti < result.tracks.size(); ++ti) {
        const auto& track = result.tracks[ti];
        const std::string key = normalizedKey(track.artist);
        auto found = artistMap.find(key);
        size_t ai = 0;
        if (found == artistMap.end()) {
            ai = result.artists.size();
            artistMap[key] = ai;
            Artist artist{};
            artist.name = track.artist;
            result.artists.push_back(std::move(artist));
        } else {
            ai = found->second;
        }
        result.artists[ai].tracks.push_back(ti);
        const std::string albumKey = normalizedKey(track.artist) + "\n" + normalizedKey(track.album);
        auto albumIt = albumByKey.find(albumKey);
        if (albumIt != albumByKey.end()) {
            auto& albums = result.artists[ai].albums;
            if (std::find(albums.begin(), albums.end(), albumIt->second) == albums.end())
                albums.push_back(albumIt->second);
        }
    }
    std::sort(result.artists.begin(), result.artists.end(), [](const Artist& a, const Artist& b) {
        return lower(a.name) < lower(b.name);
    });

    result.recentAlbums.resize(result.albums.size());
    for (size_t i = 0; i < result.albums.size(); ++i) result.recentAlbums[i] = i;
    std::stable_sort(result.recentAlbums.begin(), result.recentAlbums.end(), [&](size_t a, size_t b) {
        return result.albums[a].newestStamp > result.albums[b].newestStamp;
    });

    DebugLog::log("[music-library] scan complete tracks=%zu albums=%zu artists=%zu",
                  result.tracks.size(), result.albums.size(), result.artists.size());
    return result;
}

std::vector<size_t> MusicLibrary::searchTracks(const LibrarySnapshot& library,
                                               const std::string& query) {
    const std::string q = normalizedKey(query);
    std::vector<size_t> out;
    if (q.empty()) return out;
    for (size_t i = 0; i < library.tracks.size(); ++i) {
        const auto& track = library.tracks[i];
        const std::string hay = normalizedKey(track.title) + "\n" +
                                normalizedKey(track.artist) + "\n" +
                                normalizedKey(track.album);
        if (hay.find(q) != std::string::npos)
            out.push_back(i);
    }
    return out;
}

} // namespace switchu::menu::music
