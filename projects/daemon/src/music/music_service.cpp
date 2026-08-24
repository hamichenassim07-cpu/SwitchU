#include "music_service.hpp"
#include "stream_decoder.hpp"

#include <switchu/file_log.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace switchu::daemon::music {
namespace {

constexpr uint64_t kPersistIntervalNs = 2'000'000'000ULL;
constexpr uint64_t kNextSoonThresholdMs = 10'000ULL;
constexpr int kOutputRate = PcmResampler::kOutputRate;
constexpr int kOutputChannels = PcmResampler::kOutputChannels;
constexpr SDL_AudioFormat kOutputFormat = AUDIO_S16SYS;
constexpr uint32_t kBytesPerOutputFrame = kOutputChannels * sizeof(int16_t);
constexpr uint32_t kStartFrames = 1024;     // ~21 ms @ 48 kHz
constexpr uint32_t kSteadyFrames = 12000;   // 250 ms safety queue
constexpr uint32_t kDecodeFrames = 4096;
constexpr uint32_t kRenderFrames = 2048;

uint64_t nowTick() {
    return armGetSystemTick();
}

uint64_t elapsedMs(uint64_t fromTick) {
    if (fromTick == 0) return 0;
    return armTicksToNs(nowTick() - fromTick) / 1'000'000ULL;
}

void persistentMusicTrace(const char* fmt, ...) {
    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);
    FILE* file = std::fopen("sdmc:/config/SwitchU/music_switch_trace.log", "a");
    if (!file) return;

    std::fprintf(file, "[tick=%llu] ", static_cast<unsigned long long>(nowTick()));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool parseHexTitleId(std::string value, uint64_t& out) {
    value = trim(value);
    const auto comment = value.find('#');
    if (comment != std::string::npos) value = trim(value.substr(0, comment));
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) value.erase(0, 2);
    if (value.empty() || value.size() > 16) return false;
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    try {
        out = std::stoull(value, nullptr, 16);
        return out != 0;
    } catch (...) {
        out = 0;
        return false;
    }
}

} // namespace

MusicService::MusicService()
    : m_rng(static_cast<uint32_t>(armGetSystemTick() ^ 0x51C0A11u)) {}

MusicService::~MusicService() {
    shutdown();
}

MusicService& service() {
    static MusicService instance;
    return instance;
}

bool MusicService::initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return true;

    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);
    loadPersistedStateLocked();
    loadBlacklistLocked();
    reloadQueue();

    // A daemon restart has no live PCM device/source yet. Preserve the queue
    // and stored position, but never expose a phantom active session to HOME.
    if (m_sessionActive) {
        m_sessionActive = false;
        m_playing = false;
        m_paused = false;
        writeSessionFlagLocked(false);
    }

    m_initialized = true;
    switchu::FileLog::log(
        "[music] native PCM engine initialized queue=%zu current=%d session=%d position=%lums",
        m_queue.size(), m_currentIndex, m_sessionActive ? 1 : 0,
        static_cast<unsigned long>(m_positionBaseMs));
    persistentMusicTrace("ENGINE PCM_V1 initialized queue=%zu current=%d", m_queue.size(), m_currentIndex);
    return true;
}

void MusicService::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized && !m_audioReady) return;

    persistLocked(true);
    closeAudioLocked();
    m_sessionActive = false;
    m_playing = false;
    m_paused = false;
    writeSessionFlagLocked(false);
    m_initialized = false;
    switchu::FileLog::log("[music] native PCM engine shutdown");
    persistentMusicTrace("ENGINE shutdown");
}

bool MusicService::ensureAudioLocked() {
    if (m_audioReady && m_audioDevice != 0) return true;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0 && SDL_Init(SDL_INIT_AUDIO) < 0) {
            m_lastError = 0x2001;
            switchu::FileLog::log("[music] SDL audio init FAIL: %s", SDL_GetError());
            persistentMusicTrace("AUDIO init FAIL %s", SDL_GetError());
            return false;
        }
    }

    SDL_AudioSpec wanted{};
    wanted.freq = kOutputRate;
    wanted.format = kOutputFormat;
    wanted.channels = kOutputChannels;
    wanted.samples = 2048;
    wanted.callback = nullptr; // queued-audio mode; no user audio thread callback

    SDL_AudioSpec obtained{};
    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (m_audioDevice == 0) {
        m_lastError = 0x2002;
        switchu::FileLog::log("[music] SDL_OpenAudioDevice FAIL: %s", SDL_GetError());
        persistentMusicTrace("AUDIO open FAIL %s", SDL_GetError());
        return false;
    }

    if (obtained.freq != kOutputRate || obtained.format != kOutputFormat ||
        obtained.channels != kOutputChannels) {
        switchu::FileLog::log(
            "[music] unsupported SDL audio format got=%dHz fmt=0x%X ch=%u",
            obtained.freq, obtained.format, obtained.channels);
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
        m_lastError = 0x2003;
        return false;
    }

    m_audioSpec = obtained;
    SDL_PauseAudioDevice(m_audioDevice, 1);
    m_audioReady = true;
    m_lastError = 0;
    switchu::FileLog::log("[music] PCM AudioOut bridge opened 48kHz stereo S16 queue-mode");
    persistentMusicTrace("AUDIO open device=%u 48000/stereo/s16", static_cast<unsigned>(m_audioDevice));
    return true;
}

void MusicService::resetSourceLocked(bool clearQueuedAudio) {
    if (m_audioReady && m_audioDevice != 0) {
        SDL_PauseAudioDevice(m_audioDevice, 1);
        if (clearQueuedAudio) SDL_ClearQueuedAudio(m_audioDevice);
    }
    m_decoder.reset();
    m_resampler.reset();
    m_sourceEof = false;
    m_decodeScratch.clear();
    m_outputScratch.clear();
}

void MusicService::closeAudioLocked() {
    if (!m_audioReady && m_audioDevice == 0) {
        m_decoder.reset();
        m_preloadedDecoder.reset();
        m_preloadedIndex = -1;
        return;
    }

    persistentMusicTrace("AUDIO_CLOSE begin device=%u", static_cast<unsigned>(m_audioDevice));
    resetSourceLocked(true);
    m_preloadedDecoder.reset();
    m_preloadedIndex = -1;
    if (m_audioDevice != 0) {
        SDL_CloseAudioDevice(m_audioDevice);
        m_audioDevice = 0;
    }
    m_audioReady = false;
    persistentMusicTrace("AUDIO_CLOSE done");
}

bool MusicService::pumpAudioLocked(uint32_t targetQueuedBytes) {
    if (!m_audioReady || m_audioDevice == 0 || !m_decoder) return false;

    const int srcChannels = m_decoder->channels();
    if (srcChannels <= 0 || m_decoder->sampleRate() <= 0) return false;

    if (m_decodeScratch.size() < static_cast<size_t>(kDecodeFrames) * srcChannels)
        m_decodeScratch.resize(static_cast<size_t>(kDecodeFrames) * srcChannels);
    if (m_outputScratch.size() < static_cast<size_t>(kRenderFrames) * 2)
        m_outputScratch.resize(static_cast<size_t>(kRenderFrames) * 2);

    uint32_t queued = SDL_GetQueuedAudioSize(m_audioDevice);
    int guard = 0;
    while (queued < targetQueuedBytes && guard++ < 32) {
        const size_t produced = m_resampler.render(m_outputScratch.data(), kRenderFrames);
        if (produced > 0) {
            if (m_volume < 0.999f) {
                for (size_t i = 0; i < produced * 2; ++i) {
                    const float scaled = static_cast<float>(m_outputScratch[i]) * m_volume;
                    m_outputScratch[i] = static_cast<int16_t>(
                        std::clamp(std::lrint(scaled), -32768L, 32767L));
                }
            }
            const uint32_t bytes = static_cast<uint32_t>(produced * kBytesPerOutputFrame);
            if (SDL_QueueAudio(m_audioDevice, m_outputScratch.data(), bytes) != 0) {
                m_lastError = 0x2006;
                switchu::FileLog::log("[music] SDL_QueueAudio FAIL: %s", SDL_GetError());
                persistentMusicTrace("PCM queue FAIL %s", SDL_GetError());
                return false;
            }
            queued += bytes;
            continue;
        }

        if (m_sourceEof) break;

        const size_t decoded = m_decoder->readFrames(m_decodeScratch.data(), kDecodeFrames);
        if (decoded == 0) {
            if (m_decoder->failed()) {
                m_lastError = 0x2005;
                switchu::FileLog::log("[music] decoder FAIL: %s", m_decoder->error().c_str());
                persistentMusicTrace("DECODER read FAIL index=%d error=%s",
                                     m_currentIndex, m_decoder->error().c_str());
                return false;
            }
            m_sourceEof = true;
            m_resampler.markFinished();
            persistentMusicTrace("DECODER eof index=%d queued=%u", m_currentIndex, queued);
            continue;
        }

        m_resampler.append(m_decodeScratch.data(), decoded);
    }
    return true;
}

bool MusicService::reloadQueue() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ifstream file(switchu::music::kQueuePath, std::ios::binary);
    if (!file.is_open()) {
        m_queue.clear();
        if (m_currentIndex >= 0) m_currentIndex = -1;
        m_preloadedDecoder.reset();
        m_preloadedIndex = -1;
        return false;
    }

    switchu::music::QueueFileHeader header{};
    if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        header.magic != switchu::music::kQueueMagic ||
        header.version != switchu::music::kProtocolVersion ||
        header.count > 8192) {
        switchu::FileLog::log("[music] queue file invalid");
        return false;
    }

    std::vector<QueueEntry> parsed;
    parsed.reserve(header.count);
    for (uint32_t i = 0; i < header.count; ++i) {
        switchu::music::QueueEntryHeader eh{};
        if (!file.read(reinterpret_cast<char*>(&eh), sizeof(eh))) break;
        if (eh.path_len == 0 || eh.path_len > 4096 || eh.title_len > 4096 || eh.artist_len > 4096) {
            switchu::FileLog::log("[music] queue entry %u has invalid string lengths", i);
            break;
        }

        QueueEntry entry{};
        entry.trackId = eh.track_id;
        entry.durationMs = eh.duration_ms;
        entry.path.resize(eh.path_len);
        entry.title.resize(eh.title_len);
        entry.artist.resize(eh.artist_len);
        if (!file.read(entry.path.data(), static_cast<std::streamsize>(entry.path.size()))) break;
        if (!entry.title.empty() &&
            !file.read(entry.title.data(), static_cast<std::streamsize>(entry.title.size()))) break;
        if (!entry.artist.empty() &&
            !file.read(entry.artist.data(), static_cast<std::streamsize>(entry.artist.size()))) break;
        parsed.push_back(std::move(entry));
    }

    if (parsed.size() != header.count) {
        switchu::FileLog::log("[music] queue truncated expected=%u actual=%zu", header.count, parsed.size());
        return false;
    }

    const uint64_t oldTrackId =
        (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_queue.size()))
            ? m_queue[static_cast<size_t>(m_currentIndex)].trackId : 0;

    m_queue = std::move(parsed);
    m_volume = std::clamp(header.volume, 0.f, 1.f);
    m_shuffle = header.shuffle != 0;
    if (header.repeat_mode <= static_cast<uint8_t>(switchu::music::RepeatMode::Track))
        m_repeat = static_cast<switchu::music::RepeatMode>(header.repeat_mode);

    int requested = header.current_index;
    if (oldTrackId != 0) {
        for (size_t i = 0; i < m_queue.size(); ++i) {
            if (m_queue[i].trackId == oldTrackId) {
                requested = static_cast<int>(i);
                break;
            }
        }
    }
    if (requested < 0 || requested >= static_cast<int>(m_queue.size()))
        requested = m_queue.empty() ? -1 : 0;
    m_currentIndex = requested;

    // Queue replacement invalidates any source prepared from an older list.
    m_preloadedDecoder.reset();
    m_preloadedIndex = -1;
    switchu::FileLog::log("[music] queue loaded count=%zu current=%d", m_queue.size(), m_currentIndex);
    return true;
}

bool MusicService::loadTrackLocked(int index, bool autoplay, uint64_t startMs) {
    if (index < 0 || index >= static_cast<int>(m_queue.size())) return false;
    if (!ensureAudioLocked()) return false;

    const auto& entry = m_queue[static_cast<size_t>(index)];
    persistentMusicTrace("PCM_SWITCH enter from=%d to=%d id=%016lX session=%d queued=%u",
                         m_currentIndex, index, static_cast<unsigned long>(entry.trackId),
                         m_sessionActive ? 1 : 0,
                         m_audioDevice ? SDL_GetQueuedAudioSize(m_audioDevice) : 0);
    switchu::FileLog::log("[music-diag] PCM_SWITCH prepare from=%d to=%d id=%016lX path=%s",
                          m_currentIndex, index, static_cast<unsigned long>(entry.trackId),
                          entry.path.c_str());

    // Open/prepare the new decoder BEFORE touching the old source or its queued
    // PCM. This is the crucial difference from Mix_Music: file parsing has no
    // ownership relationship with the live AudioOut device, so the current
    // song can continue while the next source is prepared.
    std::unique_ptr<StreamDecoder> nextDecoder;
    if (m_preloadedIndex == index && m_preloadedDecoder) {
        nextDecoder = std::move(m_preloadedDecoder);
        m_preloadedIndex = -1;
        persistentMusicTrace("PCM_SWITCH using preload to=%d", index);
    } else {
        std::string decoderError;
        nextDecoder = openStreamDecoder(entry.path, decoderError);
        if (!nextDecoder) {
            m_lastError = 0x2004;
            switchu::FileLog::log("[music] decoder open FAIL path=%s error=%s",
                                  entry.path.c_str(), decoderError.c_str());
            persistentMusicTrace("PCM_SWITCH open FAIL to=%d error=%s", index, decoderError.c_str());
            return false; // old track is untouched and keeps playing
        }
    }

    const int sourceRate = nextDecoder->sampleRate();
    const int sourceChannels = nextDecoder->channels();
    if (sourceRate <= 0 || sourceChannels <= 0) {
        m_lastError = 0x2004;
        return false;
    }

    if (startMs > 0 && !nextDecoder->seekMs(startMs)) {
        switchu::FileLog::log("[music] initial seek failed, starting at 0: %s",
                              nextDecoder->error().c_str());
        startMs = 0;
    }

    // Commit point: decoder B is valid. Pause the output for only the PCM swap,
    // discard remaining A samples, install B, prime ~21 ms, then resume the SAME
    // audio device. No Mix_LoadMUS, no Mix_HaltMusic, no device close/reopen.
    SDL_PauseAudioDevice(m_audioDevice, 1);
    SDL_ClearQueuedAudio(m_audioDevice);
    m_decoder.reset();
    m_decoder = std::move(nextDecoder);
    m_resampler.configure(sourceRate, sourceChannels);
    m_sourceEof = false;
    m_decodeScratch.clear();
    m_outputScratch.clear();

    m_currentIndex = index;
    m_positionBaseMs = std::min(startMs, entry.durationMs > 0 ? entry.durationMs : startMs);
    m_playStartTick = 0;
    m_nextSoon = false;
    m_preloadedDecoder.reset();
    m_preloadedIndex = -1;

    if (!pumpAudioLocked(kStartFrames * kBytesPerOutputFrame) ||
        SDL_GetQueuedAudioSize(m_audioDevice) == 0) {
        m_playing = false;
        m_paused = false;
        m_sessionActive = false;
        writeSessionFlagLocked(false);
        persistentMusicTrace("PCM_SWITCH prime FAIL to=%d", index);
        return false;
    }

    m_sessionActive = true;
    writeSessionFlagLocked(true);
    if (autoplay) {
        m_playing = true;
        m_paused = false;
        m_playStartTick = nowTick();
        SDL_PauseAudioDevice(m_audioDevice, 0);
    } else {
        m_playing = false;
        m_paused = true;
    }

    // Fill the larger safety queue after playback has been released. This keeps
    // the audible A->B handoff short while still protecting the 10 ms daemon loop
    // from filesystem/decode jitter during normal playback.
    if (autoplay) pumpAudioLocked(kSteadyFrames * kBytesPerOutputFrame);

    m_lastError = 0;
    persistLocked(true);
    switchu::FileLog::log(
        "[music] PCM track loaded index=%d id=%016lX autoplay=%d start=%lums src=%dHz/%dch path=%s",
        index, static_cast<unsigned long>(entry.trackId), autoplay ? 1 : 0,
        static_cast<unsigned long>(m_positionBaseMs), sourceRate, sourceChannels,
        entry.path.c_str());
    persistentMusicTrace("PCM_SWITCH started index=%d queued=%u src=%d/%d",
                         index, SDL_GetQueuedAudioSize(m_audioDevice), sourceRate, sourceChannels);
    return true;
}

bool MusicService::playIndex(int index) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return loadTrackLocked(index, true, 0);
}

void MusicService::togglePause() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive || !m_decoder) return;
    if (m_paused) resume(); else pause();
}

void MusicService::pause() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive || !m_decoder || m_paused) return;
    m_positionBaseMs = currentPositionMsLocked();
    if (m_audioDevice) SDL_PauseAudioDevice(m_audioDevice, 1);
    m_playing = false;
    m_paused = true;
    m_playStartTick = 0;
    persistLocked(true);
    persistentMusicTrace("PAUSE index=%d pos=%lums", m_currentIndex,
                         static_cast<unsigned long>(m_positionBaseMs));
}

void MusicService::resume() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive) return;
    if (!m_decoder) {
        loadTrackLocked(m_currentIndex, true, m_positionBaseMs);
        return;
    }
    if (!m_paused && m_playing) return;
    pumpAudioLocked(kStartFrames * kBytesPerOutputFrame);
    if (m_audioDevice) SDL_PauseAudioDevice(m_audioDevice, 0);
    m_paused = false;
    m_playing = true;
    m_playStartTick = nowTick();
    persistLocked(true);
    persistentMusicTrace("RESUME index=%d pos=%lums", m_currentIndex,
                         static_cast<unsigned long>(m_positionBaseMs));
}

int MusicService::nextIndexLocked(bool forward) const {
    if (m_queue.empty()) return -1;
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_queue.size())) return 0;
    if (m_shuffle && m_queue.size() > 1) return m_currentIndex;

    if (forward) {
        const int n = m_currentIndex + 1;
        if (n < static_cast<int>(m_queue.size())) return n;
        if (m_repeat == switchu::music::RepeatMode::Queue) return 0;
        return -1;
    }

    const int p = m_currentIndex - 1;
    if (p >= 0) return p;
    if (m_repeat == switchu::music::RepeatMode::Queue)
        return static_cast<int>(m_queue.size()) - 1;
    return 0;
}

void MusicService::next() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    switchu::FileLog::log("[music-diag] NEXT current=%d queue=%zu", m_currentIndex, m_queue.size());
    persistentMusicTrace("NEXT command current=%d queue=%zu", m_currentIndex, m_queue.size());
    if (m_queue.empty()) return;

    int target = -1;
    if (m_shuffle && m_queue.size() > 1) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(m_queue.size()) - 2);
        target = dist(m_rng);
        if (target >= m_currentIndex) ++target;
    } else {
        target = nextIndexLocked(true);
    }
    if (target < 0) {
        stop(false);
        return;
    }
    loadTrackLocked(target, true, 0);
}

void MusicService::previous() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    const uint64_t position = currentPositionMsLocked();
    switchu::FileLog::log("[music-diag] PREVIOUS current=%d position=%lums",
                          m_currentIndex, static_cast<unsigned long>(position));
    persistentMusicTrace("PREVIOUS command current=%d position=%lums",
                         m_currentIndex, static_cast<unsigned long>(position));
    if (m_queue.empty()) return;

    if (position > 5'000 && m_currentIndex >= 0) {
        loadTrackLocked(m_currentIndex, true, 0);
        return;
    }
    int target = nextIndexLocked(false);
    if (target < 0) target = 0;
    loadTrackLocked(target, true, 0);
}

void MusicService::seekMs(uint64_t positionMs) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive || !m_decoder || !m_audioDevice) return;

    const uint64_t duration = currentDurationMsLocked();
    if (duration > 0) positionMs = std::min(positionMs, duration);
    const bool wasPlaying = m_playing && !m_paused;

    SDL_PauseAudioDevice(m_audioDevice, 1);
    SDL_ClearQueuedAudio(m_audioDevice);
    if (!m_decoder->seekMs(positionMs)) {
        m_lastError = 0x2007;
        switchu::FileLog::log("[music] seek FAIL: %s", m_decoder->error().c_str());
        if (wasPlaying) SDL_PauseAudioDevice(m_audioDevice, 0);
        return;
    }

    m_resampler.configure(m_decoder->sampleRate(), m_decoder->channels());
    m_sourceEof = false;
    m_positionBaseMs = positionMs;
    m_playStartTick = 0;
    m_nextSoon = false;
    if (!pumpAudioLocked(kStartFrames * kBytesPerOutputFrame)) return;
    if (wasPlaying) {
        m_playing = true;
        m_paused = false;
        m_playStartTick = nowTick();
        SDL_PauseAudioDevice(m_audioDevice, 0);
        pumpAudioLocked(kSteadyFrames * kBytesPerOutputFrame);
    }
    persistLocked(true);
    persistentMusicTrace("SEEK index=%d pos=%lums", m_currentIndex,
                         static_cast<unsigned long>(positionMs));
}

void MusicService::setVolume(float volume) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_volume = std::clamp(volume, 0.f, 1.f);
    // Already queued PCM keeps its old gain for at most ~250 ms; new PCM uses
    // the new gain immediately. This avoids tearing down/rebuilding the queue.
    persistLocked(true);
}

void MusicService::setShuffle(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_shuffle = enabled;
    m_preloadedDecoder.reset();
    m_preloadedIndex = -1;
    persistLocked(true);
}

void MusicService::setRepeat(switchu::music::RepeatMode mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_repeat = mode;
    m_preloadedDecoder.reset();
    m_preloadedIndex = -1;
    persistLocked(true);
}

void MusicService::stop(bool clearSession) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_audioDevice) {
        SDL_PauseAudioDevice(m_audioDevice, 1);
        SDL_ClearQueuedAudio(m_audioDevice);
    }
    m_decoder.reset();
    m_resampler.reset();
    m_sourceEof = false;
    m_preloadedDecoder.reset();
    m_preloadedIndex = -1;
    m_playing = false;
    m_paused = false;
    m_sessionActive = false;
    m_playStartTick = 0;
    m_positionBaseMs = 0;
    m_nextSoon = false;
    writeSessionFlagLocked(false);
    if (clearSession) {
        m_currentIndex = -1;
        closeAudioLocked();
    }
    persistLocked(true);
    persistentMusicTrace("STOP clearSession=%d", clearSession ? 1 : 0);
}

uint64_t MusicService::currentDurationMsLocked() const {
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_queue.size())) return 0;
    return m_queue[static_cast<size_t>(m_currentIndex)].durationMs;
}

uint64_t MusicService::currentPositionMsLocked() const {
    if (!m_sessionActive) return 0;
    uint64_t pos = m_positionBaseMs;
    if (m_playing && !m_paused && m_playStartTick != 0) pos += elapsedMs(m_playStartTick);
    const uint64_t duration = currentDurationMsLocked();
    if (duration > 0) pos = std::min(pos, duration);
    return pos;
}

bool MusicService::hasNextLocked() const {
    if (m_queue.empty() || m_currentIndex < 0) return false;
    if (m_repeat == switchu::music::RepeatMode::Track) return true;
    if (m_shuffle && m_queue.size() > 1) return true;
    if (m_currentIndex + 1 < static_cast<int>(m_queue.size())) return true;
    return m_repeat == switchu::music::RepeatMode::Queue && !m_queue.empty();
}

void MusicService::updateNextSoonLocked() {
    if (!m_sessionActive || !m_playing || m_paused || !hasNextLocked()) {
        m_nextSoon = false;
        m_preloadedDecoder.reset();
        m_preloadedIndex = -1;
        return;
    }

    const uint64_t duration = currentDurationMsLocked();
    const uint64_t position = currentPositionMsLocked();
    m_nextSoon = duration > position && (duration - position) <= kNextSoonThresholdMs;

    // Pre-open only deterministic sequential next tracks. The live decoder and
    // AudioOut queue continue untouched while this happens. At natural EOF the
    // actual handoff then only needs a PCM swap + tiny priming buffer.
    if (m_nextSoon && !m_shuffle && m_repeat != switchu::music::RepeatMode::Track &&
        !m_preloadedDecoder) {
        const int target = nextIndexLocked(true);
        if (target >= 0 && target < static_cast<int>(m_queue.size()) && target != m_currentIndex) {
            std::string error;
            auto prepared = openStreamDecoder(m_queue[static_cast<size_t>(target)].path, error);
            if (prepared) {
                m_preloadedIndex = target;
                m_preloadedDecoder = std::move(prepared);
                persistentMusicTrace("PRELOAD ready current=%d next=%d", m_currentIndex, target);
            } else {
                persistentMusicTrace("PRELOAD fail target=%d error=%s", target, error.c_str());
            }
        }
    }
}

void MusicService::handleFinishedLocked() {
    if (!m_sessionActive || m_queue.empty()) return;
    persistentMusicTrace("FINISH index=%d", m_currentIndex);

    if (m_repeat == switchu::music::RepeatMode::Track) {
        loadTrackLocked(m_currentIndex, true, 0);
        return;
    }

    int target = -1;
    if (m_shuffle && m_queue.size() > 1) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(m_queue.size()) - 2);
        target = dist(m_rng);
        if (target >= m_currentIndex) ++target;
    } else {
        target = nextIndexLocked(true);
    }

    if (target < 0) {
        m_positionBaseMs = currentDurationMsLocked();
        m_playing = false;
        m_paused = false;
        m_sessionActive = false;
        m_playStartTick = 0;
        m_nextSoon = false;
        writeSessionFlagLocked(false);
        persistLocked(true);
        return;
    }
    loadTrackLocked(target, true, 0);
}

void MusicService::update() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized) return;

    if (m_sessionActive && m_playing && !m_paused && m_decoder && m_audioDevice) {
        if (!pumpAudioLocked(kSteadyFrames * kBytesPerOutputFrame)) {
            switchu::FileLog::log("[music] PCM pump failed; stopping active session");
            stop(false);
            return;
        }

        const uint32_t queued = SDL_GetQueuedAudioSize(m_audioDevice);
        if (m_sourceEof && m_resampler.empty() && queued == 0) {
            handleFinishedLocked();
        }
    }

    updateNextSoonLocked();
    persistLocked(false);
}

switchu::music::Status MusicService::status() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    switchu::music::Status st{};
    st.current_index = m_currentIndex;
    st.queue_count = static_cast<uint32_t>(m_queue.size());
    st.position_ms = currentPositionMsLocked();
    st.duration_ms = currentDurationMsLocked();
    st.volume = m_volume;
    st.repeat_mode = static_cast<uint8_t>(m_repeat);
    st.last_error = m_lastError;
    if (m_sessionActive && m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_queue.size()))
        st.track_id = m_queue[static_cast<size_t>(m_currentIndex)].trackId;
    if (m_sessionActive) st.flags |= switchu::music::MusicStatus_SessionActive;
    if (m_playing) st.flags |= switchu::music::MusicStatus_Playing;
    if (m_paused) st.flags |= switchu::music::MusicStatus_Paused;
    if (m_shuffle) st.flags |= switchu::music::MusicStatus_Shuffle;
    if (m_nextSoon) st.flags |= switchu::music::MusicStatus_NextSoon;
    if (m_autoPausedForGame) st.flags |= switchu::music::MusicStatus_AutoPaused;
    if (m_audioReady) st.flags |= switchu::music::MusicStatus_AudioReady;
    return st;
}

void MusicService::writeSessionFlagLocked(bool active) const {
    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);
    if (active) {
        std::ofstream flag(switchu::music::kSessionFlagPath, std::ios::trunc);
        if (flag.is_open()) flag << "SwitchU Music session active\n";
    } else {
        std::filesystem::remove(switchu::music::kSessionFlagPath, ec);
    }
}

void MusicService::persistLocked(bool force) {
    if (!m_initialized && !force) return;
    const uint64_t tick = nowTick();
    if (!force && m_lastPersistTick != 0 &&
        armTicksToNs(tick - m_lastPersistTick) < kPersistIntervalNs) return;
    m_lastPersistTick = tick;

    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);
    switchu::music::PersistedState state{};
    state.current_index = m_currentIndex;
    state.position_ms = currentPositionMsLocked();
    state.volume = m_volume;
    state.repeat_mode = static_cast<uint8_t>(m_repeat);
    if (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_queue.size()))
        state.track_id = m_queue[static_cast<size_t>(m_currentIndex)].trackId;
    if (m_sessionActive) state.flags |= switchu::music::MusicStatus_SessionActive;
    if (m_shuffle) state.flags |= switchu::music::MusicStatus_Shuffle;

    const std::string tmpPath = std::string(switchu::music::kStatePath) + ".tmp";
    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;
    file.write(reinterpret_cast<const char*>(&state), sizeof(state));
    file.close();
    if (!file) return;
    std::filesystem::remove(switchu::music::kStatePath, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, switchu::music::kStatePath, ec);
}

void MusicService::loadPersistedStateLocked() {
    std::ifstream file(switchu::music::kStatePath, std::ios::binary);
    if (!file.is_open()) return;
    switchu::music::PersistedState state{};
    if (!file.read(reinterpret_cast<char*>(&state), sizeof(state)) ||
        state.magic != switchu::music::kStateMagic ||
        state.version != switchu::music::kProtocolVersion) return;
    m_currentIndex = state.current_index;
    m_positionBaseMs = state.position_ms;
    m_volume = std::clamp(state.volume, 0.f, 1.f);
    m_shuffle = (state.flags & switchu::music::MusicStatus_Shuffle) != 0;
    m_sessionActive = (state.flags & switchu::music::MusicStatus_SessionActive) != 0;
    if (state.repeat_mode <= static_cast<uint8_t>(switchu::music::RepeatMode::Track))
        m_repeat = static_cast<switchu::music::RepeatMode>(state.repeat_mode);
}

void MusicService::loadBlacklistLocked() {
    m_blacklist.clear();
    std::ifstream file(switchu::music::kBlacklistPath);
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        uint64_t tid = 0;
        if (parseHexTitleId(line, tid)) m_blacklist.insert(tid);
    }
    switchu::FileLog::log("[music] blacklist loaded entries=%zu", m_blacklist.size());
}

bool MusicService::isBlacklistedLocked(uint64_t titleId) const {
    return titleId != 0 && m_blacklist.find(titleId) != m_blacklist.end();
}

void MusicService::onGameLaunching(uint64_t titleId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    loadBlacklistLocked();
    const bool blacklisted = isBlacklistedLocked(titleId);
    switchu::FileLog::log(
        "[music-diag] OPEN_GAME title=%016lX blacklisted=%d session=%d playing=%d paused=%d",
        static_cast<unsigned long>(titleId), blacklisted ? 1 : 0,
        m_sessionActive ? 1 : 0, m_playing ? 1 : 0, m_paused ? 1 : 0);
    if (!blacklisted || !m_sessionActive || !m_decoder || m_paused) return;
    pause();
    m_autoPausedForGame = true;
    persistLocked(true);
    switchu::FileLog::log("[music] auto-paused for blacklisted title=%016lX",
                          static_cast<unsigned long>(titleId));
}

void MusicService::onGameEnded() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    switchu::FileLog::log("[music-diag] GAME_ENDED autoPaused=%d session=%d current=%d",
                          m_autoPausedForGame ? 1 : 0, m_sessionActive ? 1 : 0, m_currentIndex);
    if (!m_autoPausedForGame) return;
    m_autoPausedForGame = false;
    if (!m_sessionActive) return;
    resume();
    switchu::FileLog::log("[music] resumed after blacklisted title closed");
}

} // namespace switchu::daemon::music
