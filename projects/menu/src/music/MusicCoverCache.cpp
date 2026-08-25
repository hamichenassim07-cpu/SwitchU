#include "MusicCoverCache.hpp"

#include "core/DebugLog.hpp"
#include <switchu/music_protocol.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/third_party/stb/stb_image.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace switchu::menu::music {

namespace {
constexpr uint64_t kMaxEmbeddedCoverBytes = 6ULL * 1024ULL * 1024ULL;
constexpr uint64_t kMaxStandaloneCoverBytes = 12ULL * 1024ULL * 1024ULL;
constexpr int kBackTextureSide = 96;
constexpr size_t kMaxPersistentStyles = 96;
constexpr uint64_t kMaxPersistentBackBytes = 6ULL * 1024ULL * 1024ULL;
constexpr uint64_t kPersistentStyleMaxAgeSec = 120ULL * 24ULL * 60ULL * 60ULL;
constexpr uint64_t kPersistTouchIntervalSec = 60ULL;
constexpr const char* kStyleCachePath = "sdmc:/config/SwitchU/music/artwork_style_cache_v7.json";
constexpr const char* kBackCacheDirectory = "sdmc:/config/SwitchU/music/artwork_back_cache";

uint64_t epochSeconds() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return seconds > 0 ? static_cast<uint64_t>(seconds) : 0;
}

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
    if (ref.embedded) {
        if (ref.size == 0 || ref.size > kMaxEmbeddedCoverBytes) return {};
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end <= 0) return {};
        const uint64_t fileBytes = static_cast<uint64_t>(end);
        if (ref.offset > fileBytes || ref.size > fileBytes - ref.offset) return {};
        offset = ref.offset;
        size = ref.size;
    } else {
        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        if (end <= 0) return {};
        size = static_cast<uint64_t>(end);
        if (size > kMaxStandaloneCoverBytes) return {};
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

uint64_t fnv1a64(const std::string& text, uint64_t seed = 1469598103934665603ULL) {
    return fnv1a64(text.data(), text.size(), seed);
}

std::string hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

nxui::Color hsvToRgb(float h, float s, float v) {
    h -= std::floor(h);
    s = std::clamp(s,0.f,1.f);
    v = std::clamp(v,0.f,1.f);
    const float c=v*s, hp=h*6.f, x=c*(1.f-std::abs(std::fmod(hp,2.f)-1.f));
    float r=0,g=0,b=0;
    if(hp<1){r=c;g=x;} else if(hp<2){r=x;g=c;} else if(hp<3){g=c;b=x;}
    else if(hp<4){g=x;b=c;} else if(hp<5){r=x;b=c;} else {r=c;b=x;}
    const float m=v-c;
    return {r+m,g+m,b+m,1.f};
}

void rgbToHsv(float r,float g,float b,float& h,float& s,float& v) {
    const float mx=std::max({r,g,b}), mn=std::min({r,g,b}), d=mx-mn;
    v=mx; s=mx<=0.f?0.f:d/mx;
    if(d<1e-5f){h=0.f;return;}
    if(mx==r) h=std::fmod((g-b)/d,6.f)/6.f;
    else if(mx==g) h=((b-r)/d+2.f)/6.f;
    else h=((r-g)/d+4.f)/6.f;
    if(h<0.f) h+=1.f;
}

void boxBlurRgb(std::vector<uint8_t>& rgb, int side, int radius) {
    if (side <= 1 || radius <= 0 || rgb.size() != static_cast<size_t>(side*side*3)) return;
    std::vector<uint8_t> temp(rgb.size());

    for (int y=0; y<side; ++y) {
        for (int x=0; x<side; ++x) {
            int sum[3] = {0,0,0};
            int count = 0;
            for (int k=-radius; k<=radius; ++k) {
                const int sx = std::clamp(x+k,0,side-1);
                const size_t i=(static_cast<size_t>(y)*side+sx)*3u;
                sum[0]+=rgb[i]; sum[1]+=rgb[i+1]; sum[2]+=rgb[i+2]; ++count;
            }
            const size_t o=(static_cast<size_t>(y)*side+x)*3u;
            temp[o]=static_cast<uint8_t>(sum[0]/count);
            temp[o+1]=static_cast<uint8_t>(sum[1]/count);
            temp[o+2]=static_cast<uint8_t>(sum[2]/count);
        }
    }
    for (int y=0; y<side; ++y) {
        for (int x=0; x<side; ++x) {
            int sum[3] = {0,0,0};
            int count = 0;
            for (int k=-radius; k<=radius; ++k) {
                const int sy = std::clamp(y+k,0,side-1);
                const size_t i=(static_cast<size_t>(sy)*side+x)*3u;
                sum[0]+=temp[i]; sum[1]+=temp[i+1]; sum[2]+=temp[i+2]; ++count;
            }
            const size_t o=(static_cast<size_t>(y)*side+x)*3u;
            rgb[o]=static_cast<uint8_t>(sum[0]/count);
            rgb[o+1]=static_cast<uint8_t>(sum[1]/count);
            rgb[o+2]=static_cast<uint8_t>(sum[2]/count);
        }
    }
}

bool writeTga24(const std::string& path, const std::vector<uint8_t>& rgb, int side) {
    if (side <= 0 || rgb.size() != static_cast<size_t>(side*side*3)) return false;
    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);

    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    std::array<uint8_t,18> header{};
    header[2]=2; // uncompressed true-colour
    header[12]=static_cast<uint8_t>(side & 0xff);
    header[13]=static_cast<uint8_t>((side >> 8) & 0xff);
    header[14]=static_cast<uint8_t>(side & 0xff);
    header[15]=static_cast<uint8_t>((side >> 8) & 0xff);
    header[16]=24;
    header[17]=0x20; // top-left origin
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    for (int y=0; y<side; ++y) {
        for (int x=0; x<side; ++x) {
            const size_t i=(static_cast<size_t>(y)*side+x)*3u;
            const uint8_t bgr[3]={rgb[i+2],rgb[i+1],rgb[i]};
            out.write(reinterpret_cast<const char*>(bgr),3);
        }
    }
    out.flush();
    if (!out.good()) { out.close(); std::filesystem::remove(tmp,ec); return false; }
    out.close();
    std::filesystem::remove(path,ec);
    ec.clear();
    std::filesystem::rename(tmp,path,ec);
    if (ec) { std::filesystem::remove(tmp,ec); return false; }
    return true;
}

std::string generateBackTexture(const CoverRef& ref, const stbi_uc* pixels,
                                int w, int h, uint64_t signature,
                                const std::atomic<bool>* cancelRequested) {
    if (!pixels || w <= 0 || h <= 0) return {};
    std::vector<uint8_t> rgb(static_cast<size_t>(kBackTextureSide*kBackTextureSide*3));
    for (int y=0; y<kBackTextureSide; ++y) {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) return {};
        for (int x=0; x<kBackTextureSide; ++x) {
            // Four-point box sample gives a stable downsample before the blur.
            const float fx=(x+0.5f)*w/kBackTextureSide;
            const float fy=(y+0.5f)*h/kBackTextureSide;
            const int sx0=std::clamp(static_cast<int>(fx-w/(2.f*kBackTextureSide)),0,w-1);
            const int sx1=std::clamp(static_cast<int>(fx+w/(2.f*kBackTextureSide)),0,w-1);
            const int sy0=std::clamp(static_cast<int>(fy-h/(2.f*kBackTextureSide)),0,h-1);
            const int sy1=std::clamp(static_cast<int>(fy+h/(2.f*kBackTextureSide)),0,h-1);
            const int xs[2]={sx0,sx1}, ys[2]={sy0,sy1};
            float r=0,g=0,b=0;
            for (int yy:ys) for (int xx:xs) {
                const size_t i=(static_cast<size_t>(yy)*w+xx)*4u;
                r+=pixels[i]/255.f; g+=pixels[i+1]/255.f; b+=pixels[i+2]/255.f;
            }
            r*=0.25f; g*=0.25f; b*=0.25f;
            const float l=0.2126f*r+0.7152f*g+0.0722f*b;
            constexpr float saturation=0.48f;
            constexpr float darkness=0.43f;
            r=(l+(r-l)*saturation)*darkness;
            g=(l+(g-l)*saturation)*darkness;
            b=(l+(b-l)*saturation)*darkness;
            const size_t o=(static_cast<size_t>(y)*kBackTextureSide+x)*3u;
            rgb[o]=static_cast<uint8_t>(std::clamp(r,0.f,1.f)*255.f+0.5f);
            rgb[o+1]=static_cast<uint8_t>(std::clamp(g,0.f,1.f)*255.f+0.5f);
            rgb[o+2]=static_cast<uint8_t>(std::clamp(b,0.f,1.f)*255.f+0.5f);
        }
    }
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) return {};
    boxBlurRgb(rgb,kBackTextureSide,5);
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) return {};
    boxBlurRgb(rgb,kBackTextureSide,4);

    const uint64_t keyHash=fnv1a64(ref.key());
    const std::string path=std::string(kBackCacheDirectory)+"/back_"+hex64(keyHash)+"_"+hex64(signature)+".tga";
    std::error_code ec;
    if (std::filesystem::exists(path,ec)) return path;
    return writeTga24(path,rgb,kBackTextureSide) ? path : std::string{};
}

std::array<float,4> colorArray(const nxui::Color& c) { return {c.r,c.g,c.b,c.a}; }

nxui::Color colorFromJson(const nlohmann::json& j, const nxui::Color& fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    try {
        return {j[0].get<float>(),j[1].get<float>(),j[2].get<float>(),
                j.size()>3 ? j[3].get<float>() : 1.f};
    } catch (...) { return fallback; }
}
} // namespace

uint64_t MusicCoverCache::coverSignature(const CoverRef& ref) {
    if (!ref.valid()) return 0;
    uint64_t h=fnv1a64(ref.key());
    std::error_code ec;
    const uint64_t fileSize=static_cast<uint64_t>(std::filesystem::file_size(ref.path,ec));
    if (!ec) h=fnv1a64(&fileSize,sizeof(fileSize),h);
    ec.clear();
    const auto stamp=std::filesystem::last_write_time(ref.path,ec);
    if (!ec) {
        const auto count=stamp.time_since_epoch().count();
        h=fnv1a64(&count,sizeof(count),h);
    }
    return h;
}

MusicArtworkStyle MusicCoverCache::sampleArtworkStyle(const CoverRef& ref, uint64_t signature,
                                                        const std::atomic<bool>* cancelRequested) {
    MusicArtworkStyle out{};
    out.signature=signature;
    out.lastUsedEpochSec=epochSeconds();
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) return out;
    std::vector<uint8_t> bytes = readCoverBytes(ref);
    if (bytes.empty()) return out;

    int preW=0,preH=0;
    if (!preflightArtwork(bytes, preW, preH)) {
        DebugLog::log("[music-style] artwork preflight rejected path=%s embedded=%d offset=%llu size=%llu",
                      ref.path.c_str(), ref.embedded ? 1 : 0,
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
        return out;
    }
    int w=0,h=0,c=0;
    stbi_uc* pixels=stbi_load_from_memory(bytes.data(),static_cast<int>(bytes.size()),&w,&h,&c,4);
    if(!pixels||w<=0||h<=0){ if(pixels) stbi_image_free(pixels); return out; }

    double rr=0,gg=0,bb=0,weightSum=0;
    double rawR=0,rawG=0,rawB=0,rawWeight=0;
    const int step=std::max(1,std::max(w,h)/72);
    for(int y=0;y<h;y+=step){
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            stbi_image_free(pixels);
            return MusicArtworkStyle{};
        }
        for(int x=0;x<w;x+=step){
            const size_t i=(static_cast<size_t>(y)*w+x)*4u;
            const float a=pixels[i+3]/255.f;
            if(a<0.35f) continue;
            const float r=pixels[i]/255.f,g=pixels[i+1]/255.f,b=pixels[i+2]/255.f;
            rawR+=r*a; rawG+=g*a; rawB+=b*a; rawWeight+=a;
            const float mx=std::max({r,g,b}),mn=std::min({r,g,b});
            const float chroma=mx-mn;
            const float luma=0.2126f*r+0.7152f*g+0.0722f*b;
            if(luma<0.055f||luma>0.965f) continue;
            float wgt=(0.18f+chroma*2.35f)*(0.48f+0.52f*(1.f-std::abs(luma-0.56f)));
            if(chroma<0.045f) wgt*=0.20f;
            rr+=r*wgt;gg+=g*wgt;bb+=b*wgt;weightSum+=wgt;
        }
    }

    float baseR=0.54f,baseG=0.72f,baseB=1.f;
    if(weightSum>1e-5) {
        baseR=static_cast<float>(rr/weightSum);
        baseG=static_cast<float>(gg/weightSum);
        baseB=static_cast<float>(bb/weightSum);
    } else if (rawWeight>1e-5) {
        baseR=static_cast<float>(rawR/rawWeight);
        baseG=static_cast<float>(rawG/rawWeight);
        baseB=static_cast<float>(rawB/rawWeight);
    }

    float hue=0,sat=0,val=0;
    rgbToHsv(baseR,baseG,baseB,hue,sat,val);
    if (sat < 0.06f) sat=0.10f; // monochrome art keeps a restrained local tint.
    sat=std::clamp(sat*1.14f,0.18f,0.82f);
    val=std::clamp(val*1.10f,0.58f,0.88f);
    out.accent=hsvToRgb(hue,sat,val);
    out.spine={0.035f+out.accent.r*0.26f,0.038f+out.accent.g*0.26f,
               0.046f+out.accent.b*0.26f,1.f};
    out.back={0.018f+out.accent.r*0.105f,0.020f+out.accent.g*0.105f,
              0.026f+out.accent.b*0.105f,1.f};
    out.backTexturePath=generateBackTexture(ref,pixels,w,h,signature,cancelRequested);
    out.sampled=true;
    stbi_image_free(pixels);
    return out;
}

bool MusicCoverCache::loadTexture(const CoverRef& ref, nxui::Renderer& ren,
                                  nxui::Texture& out, int maxSide) {
    if (!ref.valid()) return false;
    if (!ref.embedded)
        return out.loadFromFile(ren.gpu(), ren, ref.path, maxSide);

    // SAFE-ENTRY: validate embedded APIC bytes before handing them to the GPU
    // image decoder. If an ID3 offset/size is malformed, reject the artwork
    // instead of attempting to decode arbitrary MP3 metadata as an image.
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
    auto found=m_map.find(key);
    if(found!=m_map.end()){
        m_lru.splice(m_lru.begin(),m_lru,found->second);
        return &found->second->texture;
    }

    Entry entry{}; entry.key=key;
    DebugLog::log("[music-diag] LOAD_COVER begin path=%s embedded=%d bytes=%llu max=%d",
                  ref.path.c_str(),ref.embedded?1:0,
                  static_cast<unsigned long long>(ref.size),qualitySide);
    if(!loadTexture(ref,ren,entry.texture,qualitySide)){
        DebugLog::log("[music-cover] unable to load %s offset=%llu size=%llu",
                      ref.path.c_str(),static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
        m_failed.insert(key); return nullptr;
    }
    m_lru.push_front(std::move(entry)); m_map[m_lru.front().key]=m_lru.begin();
    while(m_lru.size()>std::max<size_t>(1,m_maxEntries)){
        auto last=std::prev(m_lru.end()); m_map.erase(last->key); m_lru.erase(last);
    }
    return &m_lru.front().texture;
}

nxui::Texture* MusicCoverCache::getBack(const CoverRef& ref, nxui::Renderer& ren, int maxSide) {
    const MusicArtworkStyle style=styleFor(ref);
    if (!style.sampled || style.backTexturePath.empty()) return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(style.backTexturePath,ec)) return nullptr;
    CoverRef generated{};
    generated.path=style.backTexturePath;
    return get(generated,ren,maxSide);
}

MusicCoverCache::~MusicCoverCache() {
    // Artwork analysis must never stall HOME/menu destruction. The detached
    // worker owns only StyleWorkerState and checks this flag between expensive
    // stages; no future destructor can implicitly wait here.
    if (m_styleWorker)
        m_styleWorker->cancelRequested.store(true, std::memory_order_relaxed);
}

void MusicCoverCache::prunePersistentStyleCache() {
    const uint64_t now = epochSeconds();
    bool changed = false;

    for (auto it = m_persistedStyles.begin(); it != m_persistedStyles.end();) {
        const auto& style = it->second;
        const bool expired = now > 0 && style.lastUsedEpochSec > 0 &&
            now > style.lastUsedEpochSec + kPersistentStyleMaxAgeSec;
        if (expired) {
            it = m_persistedStyles.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    auto oldestKey = [this]() -> std::string {
        std::string key;
        uint64_t oldest = UINT64_MAX;
        for (const auto& [candidate, style] : m_persistedStyles) {
            const uint64_t stamp = style.lastUsedEpochSec;
            if (key.empty() || stamp < oldest) {
                key = candidate;
                oldest = stamp;
            }
        }
        return key;
    };

    while (m_persistedStyles.size() > kMaxPersistentStyles) {
        const std::string key = oldestKey();
        if (key.empty()) break;
        m_persistedStyles.erase(key);
        changed = true;
    }

    uint64_t backBytes = 0;
    for (const auto& [key, style] : m_persistedStyles) {
        (void)key;
        if (style.backTexturePath.empty()) continue;
        std::error_code ec;
        const uint64_t size = static_cast<uint64_t>(
            std::filesystem::file_size(style.backTexturePath, ec));
        if (!ec) backBytes += size;
    }
    while (backBytes > kMaxPersistentBackBytes && !m_persistedStyles.empty()) {
        const std::string key = oldestKey();
        if (key.empty()) break;
        const auto found = m_persistedStyles.find(key);
        if (found == m_persistedStyles.end()) break;
        if (!found->second.backTexturePath.empty()) {
            std::error_code ec;
            const uint64_t size = static_cast<uint64_t>(
                std::filesystem::file_size(found->second.backTexturePath, ec));
            if (!ec && size <= backBytes) backBytes -= size;
        }
        m_persistedStyles.erase(found);
        changed = true;
    }

    if (changed) m_persistentCacheDirty = true;
}

void MusicCoverCache::touchStyle(const std::string& key, MusicArtworkStyle& style) {
    const uint64_t now = epochSeconds();
    if (now == 0) return;
    if (style.lastUsedEpochSec == 0 || now >= style.lastUsedEpochSec + kPersistTouchIntervalSec) {
        style.lastUsedEpochSec = now;
        m_persistedStyles[key] = style;
        m_persistentCacheDirty = true;
    }
    if (m_persistentCacheDirty &&
        (m_lastPersistEpochSec == 0 || now >= m_lastPersistEpochSec + kPersistTouchIntervalSec)) {
        persistStyleCache();
    }
}

void MusicCoverCache::ensurePersistentStyleCacheLoaded() {
    if (m_persistentStylesLoaded) return;
    m_persistentStylesLoaded=true;
    m_lastPersistEpochSec=epochSeconds();
    std::ifstream in(kStyleCachePath);
    if (!in.is_open()) return;
    try {
        nlohmann::json root; in >> root;
        const int version = root.value("version",0);
        if ((version != 1 && version != 2) || !root.contains("entries") || !root["entries"].is_array()) return;
        for (const auto& entry:root["entries"]) {
            if (!entry.is_object()) continue;
            const std::string key=entry.value("key",std::string{});
            if (key.empty()) continue;
            MusicArtworkStyle style{};
            style.signature=entry.value("signature",uint64_t{0});
            style.lastUsedEpochSec=entry.value("last_used",uint64_t{0});
            if (style.lastUsedEpochSec==0) style.lastUsedEpochSec=epochSeconds();
            style.accent=colorFromJson(entry.value("accent",nlohmann::json{}),style.accent);
            style.spine=colorFromJson(entry.value("spine",nlohmann::json{}),style.spine);
            style.back=colorFromJson(entry.value("back",nlohmann::json{}),style.back);
            style.backTexturePath=entry.value("back_texture",std::string{});
            style.sampled=entry.value("sampled",false);
            if (style.sampled && style.signature!=0) m_persistedStyles[key]=std::move(style);
        }
        prunePersistentStyleCache();
        DebugLog::log("[music-style] persistent cache loaded entries=%zu",m_persistedStyles.size());
    } catch (...) {
        DebugLog::log("[music-style] persistent cache invalid; rebuilding lazily");
        m_persistedStyles.clear();
    }
}

void MusicCoverCache::persistStyleCache() {
    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory,ec);
    std::filesystem::create_directories(kBackCacheDirectory,ec);

    // Cache data is only a performance hint. Prune by age/LRU and total back
    // material bytes before committing metadata, never by unordered_map order.
    prunePersistentStyleCache();

    nlohmann::json root;
    root["version"]=2;
    root["entries"]=nlohmann::json::array();
    std::unordered_set<std::string> referencedBacks;
    for (const auto& [key,style]:m_persistedStyles) {
        nlohmann::json e;
        e["key"]=key;
        e["signature"]=style.signature;
        e["last_used"]=style.lastUsedEpochSec;
        e["accent"]=colorArray(style.accent);
        e["spine"]=colorArray(style.spine);
        e["back"]=colorArray(style.back);
        e["back_texture"]=style.backTexturePath;
        e["sampled"]=style.sampled;
        root["entries"].push_back(std::move(e));
        if (!style.backTexturePath.empty()) referencedBacks.insert(style.backTexturePath);
    }

    const std::string tmp=std::string(kStyleCachePath)+".tmp";
    {
        std::ofstream out(tmp,std::ios::trunc);
        if (!out.is_open()) return;
        out << root.dump(1);
    }
    std::filesystem::remove(kStyleCachePath,ec); ec.clear();
    std::filesystem::rename(tmp,kStyleCachePath,ec);
    if (ec) { std::filesystem::remove(tmp,ec); return; }
    m_persistentCacheDirty=false;
    m_lastPersistEpochSec=epochSeconds();

    // Remove stale/orphaned generated backs after metadata is safely committed.
    ec.clear();
    if (std::filesystem::exists(kBackCacheDirectory,ec)) {
        for (const auto& it:std::filesystem::directory_iterator(kBackCacheDirectory,ec)) {
            if (ec) break;
            if (!it.is_regular_file()) continue;
            const std::string path=it.path().string();
            if (referencedBacks.find(path)==referencedBacks.end()) {
                std::error_code removeEc; std::filesystem::remove(it.path(),removeEc);
            }
        }
    }
}

void MusicCoverCache::requestStyle(const CoverRef& ref) {
    pollStyleRequest();
    ensurePersistentStyleCacheLoaded();
    if (!ref.valid()) return;
    const std::string key=ref.key();

    if (auto existing=m_styles.find(key); existing!=m_styles.end()) {
        touchStyle(key,existing->second);
        return;
    }
    if (m_styleFailed.find(key)!=m_styleFailed.end() || m_styleRunning) return;

    const uint64_t signature=coverSignature(ref);
    if (signature!=0) {
        const auto cached=m_persistedStyles.find(key);
        if (cached!=m_persistedStyles.end() && cached->second.signature==signature) {
            bool rearMaterialUsable = true;
            if (!cached->second.backTexturePath.empty()) {
                std::error_code backEc;
                rearMaterialUsable = std::filesystem::exists(cached->second.backTexturePath, backEc) && !backEc;
            }
            if (rearMaterialUsable) {
                m_styles[key]=cached->second;
                touchStyle(key,m_styles[key]);
                return;
            }
            // Metadata without its generated rear material is stale. Resample
            // once; never fall back to the old multi-tap fake blur every boot.
            m_persistedStyles.erase(cached);
            m_persistentCacheDirty=true;
        }
    }

    m_stylePendingKey=key;
    m_styleRunning=true;
    auto worker=std::make_shared<StyleWorkerState>();
    m_styleWorker=worker;
    std::thread([worker,ref,key,signature](){
        DebugLog::log("[music-style] worker begin path=%s embedded=%d offset=%llu size=%llu",
                      ref.path.c_str(), ref.embedded ? 1 : 0,
                      static_cast<unsigned long long>(ref.offset),
                      static_cast<unsigned long long>(ref.size));
        StyleResult result{};
        result.key=key;
        result.signature=signature;
        result.style=sampleArtworkStyle(ref,signature,&worker->cancelRequested);
        DebugLog::log("[music-style] worker end path=%s sampled=%d",
                      ref.path.c_str(), result.style.sampled ? 1 : 0);
        if (worker->cancelRequested.load(std::memory_order_relaxed)) return;
        {
            std::lock_guard<std::mutex> lock(worker->resultMutex);
            worker->result.emplace(std::move(result));
        }
        worker->ready.store(true,std::memory_order_release);
    }).detach();
}

void MusicCoverCache::pollStyleRequest() {
    const auto worker=m_styleWorker;
    if(!m_styleRunning || !worker || !worker->ready.load(std::memory_order_acquire)) return;
    std::optional<StyleResult> result;
    {
        std::lock_guard<std::mutex> lock(worker->resultMutex);
        if (worker->result) result.emplace(std::move(*worker->result));
        worker->result.reset();
    }
    m_styleRunning=false;
    m_stylePendingKey.clear();
    m_styleWorker.reset();
    if (!result) return;
    if(result->style.sampled) {
        if (result->style.lastUsedEpochSec==0)
            result->style.lastUsedEpochSec=epochSeconds();
        m_styles[result->key]=result->style;
        m_persistedStyles[result->key]=result->style;
        m_persistentCacheDirty=true;
        persistStyleCache();
    } else {
        m_styleFailed.insert(result->key);
    }
}

MusicArtworkStyle MusicCoverCache::styleFor(const CoverRef& ref) const {
    if(!ref.valid()) return {};
    const auto it=m_styles.find(ref.key());
    return it==m_styles.end()?MusicArtworkStyle{}:it->second;
}

void MusicCoverCache::clear() {
    if (!m_lru.empty())
        DebugLog::log("[music-diag] UNLOAD_COVER clear count=%zu", m_lru.size());
    m_map.clear(); m_lru.clear(); m_failed.clear();
    m_styles.clear(); m_styleFailed.clear();
    // Persistent metadata/back materials intentionally survive UI cache clears.
}

} // namespace switchu::menu::music
