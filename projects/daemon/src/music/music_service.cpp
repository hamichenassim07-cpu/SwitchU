#include "music_service.hpp"

#include <switchu/file_log.hpp>

#include <algorithm>
#include <cctype>
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
constexpr uint64_t kLateFinishGuardNs = 120'000'000ULL;

uint64_t nowTick() {
    return armGetSystemTick();
}

void persistentMusicTrace(const char* fmt, ...) {
    // Independent append-only trace: unlike daemon.log this survives a daemon
    // restart and is flushed at every critical switch stage. This makes the
    // last completed audio operation recoverable even after a hard crash.
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

uint64_t elapsedMs(uint64_t fromTick) {
    if (fromTick == 0)
        return 0;
    return armTicksToNs(nowTick() - fromTick) / 1'000'000ULL;
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
    if (comment != std::string::npos)
        value = trim(value.substr(0, comment));
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0)
        value.erase(0, 2);
    if (value.empty() || value.size() > 16)
        return false;
    for (char c : value) {
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
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

std::atomic<MusicService*> MusicService::s_active{nullptr};

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
    if (m_initialized)
        return true;

    std::error_code ec;
    std::filesystem::create_directories(switchu::music::kConfigDirectory, ec);

    loadPersistedStateLocked();
    loadBlacklistLocked();
    reloadQueue();

    // FIX1: a daemon restart cannot truthfully claim a live Music session
    // because SDL_mixer has not reopened or resumed the decoder yet. Preserve
    // queue/position for diagnostics, but clear the externally visible session
    // state so HOME never creates a phantom player after a crash/restart.
    if (m_sessionActive) {
        m_sessionActive = false;
        m_playing = false;
        m_paused = false;
        writeSessionFlagLocked(false);
    }

    m_initialized = true;
    s_active.store(this);
    switchu::FileLog::log(
        "[music] service initialized queue=%zu current=%d session=%d position=%lums",
        m_queue.size(), m_currentIndex, m_sessionActive ? 1 : 0,
        static_cast<unsigned long>(m_positionBaseMs));
    return true;
}

void MusicService::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized && !m_audioReady)
        return;

    persistLocked(true);
    freeMusicLocked();
    if (!m_sessionActive)
        writeSessionFlagLocked(false);
    s_active.store(nullptr);
    m_initialized = false;
    switchu::FileLog::log("[music] service shutdown");
}

bool MusicService::ensureAudioLocked() {
    if (m_audioReady)
        return true;

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            // SDL_InitSubSystem can fail when SDL has never been initialized.
            if (SDL_Init(SDL_INIT_AUDIO) < 0) {
                m_lastError = 0x1001;
                switchu::FileLog::log("[music] SDL audio init FAIL: %s", SDL_GetError());
                return false;
            }
        }
    }

    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) < 0) {
        m_lastError = 0x1002;
        switchu::FileLog::log("[music] Mix_OpenAudio FAIL: %s", Mix_GetError());
        return false;
    }

    Mix_VolumeMusic(static_cast<int>(std::clamp(m_volume, 0.f, 1.f) * MIX_MAX_VOLUME));
    // FIX2: do not register SDL_mixer's audio-thread completion callback.
    // Completion is detected from the daemon update loop instead, eliminating
    // a cross-thread transition path shared by manual and automatic changes.
    Mix_HookMusicFinished(nullptr);
    m_audioReady = true;
    m_lastError = 0;
    switchu::FileLog::log("[music] audio session opened 48kHz stereo");
    return true;
}

void MusicService::closeAudioLocked() {
    if (!m_audioReady)
        return;
    persistentMusicTrace("AUDIO_CLOSE begin retired=%zu", m_retiredMusic.size());
    Mix_HookMusicFinished(nullptr);
    Mix_HaltMusic();
    Mix_CloseAudio();
    m_audioReady = false;
    persistentMusicTrace("AUDIO_CLOSE done");
}

void MusicService::retireCurrentMusicLocked(bool haltPlayback) {
    if (!m_music)
        return;

    // FIX2 stability quarantine: never free a decoder while the SDL audio
    // device remains active. Manual next/previous and natural completion all
    // pass through this function, so the old handle is retained until a full
    // session teardown after Mix_CloseAudio().
    m_finishedPending.store(false);
    persistentMusicTrace("RETIRE begin index=%d handle=%p halt=%d playing=%d paused=%d",
                         m_currentIndex, static_cast<void*>(m_music), haltPlayback ? 1 : 0,
                         Mix_PlayingMusic() ? 1 : 0, Mix_PausedMusic() ? 1 : 0);

    if (haltPlayback && (Mix_PlayingMusic() || Mix_PausedMusic()))
        Mix_HaltMusic();

    switchu::FileLog::log("[music-diag] RETIRE_TRACK index=%d handle=%p",
                          m_currentIndex, static_cast<void*>(m_music));
    m_retiredMusic.push_back({m_music, nowTick()});
    m_music = nullptr;
    persistentMusicTrace("RETIRE done retained=%zu", m_retiredMusic.size());
}

void MusicService::collectRetiredMusicLocked(bool force) {
    if (m_retiredMusic.empty() || !force)
        return;

    // Never call Mix_FreeMusic during an active audio session. The caller must
    // close SDL_mixer first, then force this collection. This deliberately
    // trades a small temporary decoder-retention cost for deterministic life-
    // time while we remove the hardware crash on track transitions.
    persistentMusicTrace("FREE_RETIRED begin count=%zu audioReady=%d",
                         m_retiredMusic.size(), m_audioReady ? 1 : 0);
    for (auto& retired : m_retiredMusic) {
        if (!retired.handle) continue;
        switchu::FileLog::log("[music-diag] FREE_RETIRED_TRACK handle=%p force=1",
                              static_cast<void*>(retired.handle));
        Mix_FreeMusic(retired.handle);
    }
    m_retiredMusic.clear();
    persistentMusicTrace("FREE_RETIRED done");
}

void MusicService::freeMusicLocked() {
    if (m_music) {
        switchu::FileLog::log("[music-diag] UNLOAD_TRACK begin index=%d", m_currentIndex);
        retireCurrentMusicLocked(true);
    }
    // FIX2: destroy the SDL audio device before freeing any retained decoder.
    // This gives the audio thread a hard synchronization boundary.
    closeAudioLocked();
    collectRetiredMusicLocked(true);
    switchu::FileLog::log("[music-diag] UNLOAD_TRACK done");
}

bool MusicService::reloadQueue() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Recursive locking is intentional: initialize() and queue refresh can
    // share this path safely while all decoder/queue state stays serialized.
    std::ifstream file(switchu::music::kQueuePath, std::ios::binary);
    if (!file.is_open()) {
        m_queue.clear();
        if (m_currentIndex >= 0)
            m_currentIndex = -1;
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
        if (!file.read(reinterpret_cast<char*>(&eh), sizeof(eh)))
            break;
        if (eh.path_len == 0 || eh.path_len > 4096 ||
            eh.title_len > 4096 || eh.artist_len > 4096) {
            switchu::FileLog::log("[music] queue entry %u has invalid string lengths", i);
            break;
        }

        QueueEntry entry{};
        entry.trackId = eh.track_id;
        entry.durationMs = eh.duration_ms;
        entry.path.resize(eh.path_len);
        entry.title.resize(eh.title_len);
        entry.artist.resize(eh.artist_len);
        if (!file.read(entry.path.data(), static_cast<std::streamsize>(entry.path.size())))
            break;
        if (!entry.title.empty() &&
            !file.read(entry.title.data(), static_cast<std::streamsize>(entry.title.size())))
            break;
        if (!entry.artist.empty() &&
            !file.read(entry.artist.data(), static_cast<std::streamsize>(entry.artist.size())))
            break;
        parsed.push_back(std::move(entry));
    }

    if (parsed.size() != header.count) {
        switchu::FileLog::log("[music] queue truncated expected=%u actual=%zu",
                              header.count, parsed.size());
        return false;
    }

    const uint64_t oldTrackId =
        (m_currentIndex >= 0 && m_currentIndex < static_cast<int>(m_queue.size()))
            ? m_queue[static_cast<size_t>(m_currentIndex)].trackId
            : 0;

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

    switchu::FileLog::log("[music] queue loaded count=%zu current=%d",
                          m_queue.size(), m_currentIndex);
    return true;
}

bool MusicService::loadTrackLocked(int index, bool autoplay, uint64_t startMs) {
    if (index < 0 || index >= static_cast<int>(m_queue.size()))
        return false;
    if (!ensureAudioLocked())
        return false;

    const auto& entry = m_queue[static_cast<size_t>(index)];
    const bool hadSession = m_sessionActive;
    persistentMusicTrace("SWITCH enter from=%d to=%d id=%016lX hadSession=%d retained=%zu",
                         m_currentIndex, index, static_cast<unsigned long>(entry.trackId),
                         hadSession ? 1 : 0, m_retiredMusic.size());
    switchu::FileLog::log("[music-diag] SWITCH_TRACK prepare from=%d to=%d id=%016lX path=%s",
                          m_currentIndex, index, static_cast<unsigned long>(entry.trackId),
                          entry.path.c_str());

    // Load the next decoder BEFORE destroying/retiring the current one.  If a
    // malformed file cannot be opened, the current session is left intact.
    Mix_Music* nextMusic = Mix_LoadMUS(entry.path.c_str());
    if (!nextMusic) {
        m_lastError = 0x1003;
        switchu::FileLog::log("[music] Mix_LoadMUS FAIL path=%s error=%s",
                              entry.path.c_str(), Mix_GetError());
        persistentMusicTrace("SWITCH load FAIL to=%d error=%s", index, Mix_GetError());
        return false;
    }
    persistentMusicTrace("SWITCH load done to=%d next=%p", index, static_cast<void*>(nextMusic));

    // Stop ownership of the old decoder without freeing it on the same tick as
    // the audio callback.  Deferred release is the central V0.02 crash fix.
    persistentMusicTrace("SWITCH retire-old begin from=%d", m_currentIndex);
    retireCurrentMusicLocked(true);
    persistentMusicTrace("SWITCH retire-old done retained=%zu", m_retiredMusic.size());
    m_finishedPending.store(false);
    m_lastTrackSwitchTick.store(nowTick());
    m_music = nextMusic;

    m_currentIndex = index;
    m_positionBaseMs = std::min(startMs, entry.durationMs > 0 ? entry.durationMs : startMs);
    m_playStartTick = 0;
    m_nextSoon = false;

    // Keep an already-active session logically active across a normal track
    // switch. FIX1 briefly removed/recreated session.flag on every change,
    // which made HOME react to a transient false state. First playback still
    // remains inactive until Mix_PlayMusic succeeds.
    m_sessionActive = hadSession;
    if (!hadSession)
        writeSessionFlagLocked(false);

    Mix_VolumeMusic(static_cast<int>(m_volume * MIX_MAX_VOLUME));

    if (autoplay) {
        // SDL_mixer loops=0 means play exactly once. V0.01 used 1, which
        // replayed every track a second time before advancing the queue.
        switchu::FileLog::log("[music-diag] PLAY_TRACK begin index=%d", index);
        persistentMusicTrace("PLAY begin index=%d handle=%p", index, static_cast<void*>(m_music));
        if (Mix_PlayMusic(m_music, 0) < 0) {
            m_lastError = 0x1004;
            m_playing = false;
            m_paused = false;
            m_sessionActive = false;
            writeSessionFlagLocked(false);
            switchu::FileLog::log("[music-diag] PLAY_TRACK FAIL index=%d error=%s",
                                  index, Mix_GetError());
            persistentMusicTrace("PLAY FAIL index=%d error=%s", index, Mix_GetError());
            retireCurrentMusicLocked(false);
            return false;
        }
        if (m_positionBaseMs > 0) {
            const double seconds = static_cast<double>(m_positionBaseMs) / 1000.0;
            if (Mix_SetMusicPosition(seconds) < 0) {
                switchu::FileLog::log("[music] initial seek %.3fs not supported: %s",
                                      seconds, Mix_GetError());
                m_positionBaseMs = 0;
            }
        }
        m_playing = true;
        m_paused = false;
        m_sessionActive = true;
        writeSessionFlagLocked(true);
        m_playStartTick = nowTick();
        switchu::FileLog::log("[music-diag] PLAY_TRACK started index=%d start=%lums",
                              index, static_cast<unsigned long>(m_positionBaseMs));
        persistentMusicTrace("PLAY started index=%d start=%lums retained=%zu",
                             index, static_cast<unsigned long>(m_positionBaseMs),
                             m_retiredMusic.size());
    } else {
        m_playing = false;
        m_paused = true;
        m_sessionActive = true;
        writeSessionFlagLocked(true);
    }

    m_lastError = 0;
    persistLocked(true);
    switchu::FileLog::log(
        "[music] track loaded index=%d id=%016lX autoplay=%d start=%lums path=%s",
        index,
        static_cast<unsigned long>(entry.trackId),
        autoplay ? 1 : 0,
        static_cast<unsigned long>(m_positionBaseMs),
        entry.path.c_str());
    return true;
}

bool MusicService::playIndex(int index) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return loadTrackLocked(index, true, 0);
}

void MusicService::togglePause() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive || !m_music)
        return;
    if (m_paused)
        resume();
    else
        pause();
}

void MusicService::pause() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive || !m_music || m_paused)
        return;
    m_positionBaseMs = currentPositionMsLocked();
    Mix_PauseMusic();
    m_playing = false;
    m_paused = true;
    m_playStartTick = 0;
    persistLocked(true);
}

void MusicService::resume() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive)
        return;
    if (!m_music) {
        loadTrackLocked(m_currentIndex, true, m_positionBaseMs);
        return;
    }
    if (!m_paused && m_playing)
        return;
    Mix_ResumeMusic();
    m_paused = false;
    m_playing = true;
    m_playStartTick = nowTick();
    persistLocked(true);
}

int MusicService::nextIndexLocked(bool forward) const {
    if (m_queue.empty())
        return -1;
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_queue.size()))
        return 0;

    if (m_shuffle && m_queue.size() > 1) {
        // const method cannot advance RNG; caller handles shuffle separately.
        return m_currentIndex;
    }

    if (forward) {
        const int n = m_currentIndex + 1;
        if (n < static_cast<int>(m_queue.size()))
            return n;
        if (m_repeat == switchu::music::RepeatMode::Queue)
            return 0;
        return -1;
    }

    const int p = m_currentIndex - 1;
    if (p >= 0)
        return p;
    if (m_repeat == switchu::music::RepeatMode::Queue)
        return static_cast<int>(m_queue.size()) - 1;
    return 0;
}

void MusicService::next() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_finishedPending.store(false);
    switchu::FileLog::log("[music-diag] NEXT current=%d queue=%zu", m_currentIndex, m_queue.size());
    persistentMusicTrace("NEXT command current=%d queue=%zu", m_currentIndex, m_queue.size());
    if (m_queue.empty())
        return;

    int target = -1;
    if (m_shuffle && m_queue.size() > 1) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(m_queue.size()) - 2);
        target = dist(m_rng);
        if (target >= m_currentIndex)
            ++target;
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
    m_finishedPending.store(false);
    switchu::FileLog::log("[music-diag] PREVIOUS current=%d position=%lums",
                          m_currentIndex, static_cast<unsigned long>(currentPositionMsLocked()));
    persistentMusicTrace("PREVIOUS command current=%d position=%lums",
                         m_currentIndex, static_cast<unsigned long>(currentPositionMsLocked()));
    if (m_queue.empty())
        return;

    const uint64_t position = currentPositionMsLocked();
    if (position > 5'000 && m_currentIndex >= 0) {
        loadTrackLocked(m_currentIndex, true, 0);
        return;
    }

    int target = nextIndexLocked(false);
    if (target < 0)
        target = 0;
    loadTrackLocked(target, true, 0);
}

void MusicService::seekMs(uint64_t positionMs) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_sessionActive || !m_music)
        return;

    const uint64_t duration = currentDurationMsLocked();
    if (duration > 0)
        positionMs = std::min(positionMs, duration);

    const bool wasPlaying = m_playing && !m_paused;
    const double seconds = static_cast<double>(positionMs) / 1000.0;
    switchu::FileLog::log("[music-diag] SEEK begin index=%d position=%lums playing=%d",
                          m_currentIndex, static_cast<unsigned long>(positionMs), wasPlaying ? 1 : 0);
    if (Mix_SetMusicPosition(seconds) < 0) {
        m_lastError = 0x1005;
        switchu::FileLog::log("[music] seek %.3fs FAIL: %s", seconds, Mix_GetError());
        return;
    }

    m_positionBaseMs = positionMs;
    m_playStartTick = wasPlaying ? nowTick() : 0;
    m_nextSoon = false;
    persistLocked(true);
    switchu::FileLog::log("[music-diag] SEEK done index=%d position=%lums",
                          m_currentIndex, static_cast<unsigned long>(positionMs));
}

void MusicService::setVolume(float volume) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_volume = std::clamp(volume, 0.f, 1.f);
    if (m_audioReady)
        Mix_VolumeMusic(static_cast<int>(m_volume * MIX_MAX_VOLUME));
    persistLocked(true);
}

void MusicService::setShuffle(bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_shuffle = enabled;
    persistLocked(true);
}

void MusicService::setRepeat(switchu::music::RepeatMode mode) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_repeat = mode;
    persistLocked(true);
}

void MusicService::stop(bool clearSession) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_finishedPending.store(false);

    // Do not leave a halted decoder registered with the finished callback.
    // Keeping it around was another way a late callback could restart/advance
    // the queue after an explicit stop.
    if (m_music)
        retireCurrentMusicLocked(true);

    m_playing = false;
    m_paused = false;
    m_playStartTick = 0;
    m_positionBaseMs = 0;
    m_nextSoon = false;
    if (clearSession) {
        persistentMusicTrace("STOP clear-session begin retained=%zu", m_retiredMusic.size());
        closeAudioLocked();
        collectRetiredMusicLocked(true);
        m_sessionActive = false;
        m_currentIndex = -1;
        writeSessionFlagLocked(false);
        persistentMusicTrace("STOP clear-session done");
    }
    persistLocked(true);
}

uint64_t MusicService::currentDurationMsLocked() const {
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_queue.size()))
        return 0;
    return m_queue[static_cast<size_t>(m_currentIndex)].durationMs;
}

uint64_t MusicService::currentPositionMsLocked() const {
    if (!m_sessionActive)
        return 0;
    uint64_t pos = m_positionBaseMs;
    if (m_playing && !m_paused && m_playStartTick != 0)
        pos += elapsedMs(m_playStartTick);
    const uint64_t duration = currentDurationMsLocked();
    if (duration > 0)
        pos = std::min(pos, duration);
    return pos;
}

bool MusicService::hasNextLocked() const {
    if (m_queue.empty() || m_currentIndex < 0)
        return false;
    if (m_repeat == switchu::music::RepeatMode::Track)
        return true;
    if (m_shuffle && m_queue.size() > 1)
        return true;
    if (m_currentIndex + 1 < static_cast<int>(m_queue.size()))
        return true;
    return m_repeat == switchu::music::RepeatMode::Queue && !m_queue.empty();
}

void MusicService::updateNextSoonLocked() {
    if (!m_sessionActive || !m_playing || m_paused || !hasNextLocked()) {
        m_nextSoon = false;
        return;
    }
    const uint64_t duration = currentDurationMsLocked();
    const uint64_t position = currentPositionMsLocked();
    m_nextSoon = duration > position && (duration - position) <= kNextSoonThresholdMs;
}

void MusicService::handleFinishedLocked() {
    if (!m_sessionActive || m_queue.empty())
        return;

    if (m_repeat == switchu::music::RepeatMode::Track) {
        loadTrackLocked(m_currentIndex, true, 0);
        return;
    }

    int target = -1;
    if (m_shuffle && m_queue.size() > 1) {
        std::uniform_int_distribution<int> dist(0, static_cast<int>(m_queue.size()) - 2);
        target = dist(m_rng);
        if (target >= m_currentIndex)
            ++target;
    } else {
        target = nextIndexLocked(true);
    }

    if (target < 0) {
        m_positionBaseMs = currentDurationMsLocked();
        m_playing = false;
        m_paused = false;
        m_playStartTick = 0;
        m_nextSoon = false;
        persistLocked(true);
        return;
    }
    loadTrackLocked(target, true, 0);
}

void MusicService::onMusicFinishedStatic() {
    auto* active = s_active.load();
    if (!active)
        return;

    // Legacy defensive callback path. FIX2 no longer registers this callback;
    // natural completion is polled from update() to keep transitions on one
    // serialized daemon thread. Keep the guard in case a third-party path ever
    // registers it again.
    const uint64_t switchedAt = active->m_lastTrackSwitchTick.load();
    if (switchedAt != 0 &&
        armTicksToNs(nowTick() - switchedAt) < kLateFinishGuardNs)
        return;

    active->m_finishedPending.store(true);
}

void MusicService::update() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_initialized)
        return;

    // FIX2: natural completion is polled from this serialized daemon thread.
    // SDL_mixer's Mix_HookMusicFinished callback is intentionally disabled,
    // so automatic and manual changes can no longer race through two threads.
    const uint64_t switchedAt = m_lastTrackSwitchTick.load();
    const bool outsideStartGuard = switchedAt == 0 ||
        armTicksToNs(nowTick() - switchedAt) >= 250'000'000ULL;
    if (outsideStartGuard && m_sessionActive && m_playing && !m_paused &&
        m_music && Mix_PlayingMusic() == 0) {
        m_finishedPending.store(false);
        switchu::FileLog::log("[music-diag] FINISH_EVENT index=%d source=poll", m_currentIndex);
        persistentMusicTrace("FINISH poll index=%d retained=%zu",
                             m_currentIndex, m_retiredMusic.size());
        handleFinishedLocked();
    }

    // Retired decoders are intentionally not collected here. They are only
    // released after a full Mix_CloseAudio() boundary during session teardown.
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
        if (flag.is_open())
            flag << "SwitchU Music session active\n";
    } else {
        std::filesystem::remove(switchu::music::kSessionFlagPath, ec);
    }
}

void MusicService::persistLocked(bool force) {
    if (!m_initialized && !force)
        return;
    const uint64_t tick = nowTick();
    if (!force && m_lastPersistTick != 0 &&
        armTicksToNs(tick - m_lastPersistTick) < kPersistIntervalNs)
        return;
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
    if (!file.is_open())
        return;
    file.write(reinterpret_cast<const char*>(&state), sizeof(state));
    file.close();
    if (!file)
        return;
    std::filesystem::remove(switchu::music::kStatePath, ec);
    ec.clear();
    std::filesystem::rename(tmpPath, switchu::music::kStatePath, ec);
}

void MusicService::loadPersistedStateLocked() {
    std::ifstream file(switchu::music::kStatePath, std::ios::binary);
    if (!file.is_open())
        return;
    switchu::music::PersistedState state{};
    if (!file.read(reinterpret_cast<char*>(&state), sizeof(state)) ||
        state.magic != switchu::music::kStateMagic ||
        state.version != switchu::music::kProtocolVersion)
        return;
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
    if (!file.is_open())
        return;
    std::string line;
    while (std::getline(file, line)) {
        uint64_t tid = 0;
        if (parseHexTitleId(line, tid))
            m_blacklist.insert(tid);
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
    // Default V0.02 policy: keep playing. Only explicitly incompatible
    // titles are auto-paused. Hardware audio-session arbitration may still
    // force pauses on some games; diagnostics make those cases distinguishable.
    if (!blacklisted || !m_sessionActive || !m_music || m_paused)
        return;
    m_positionBaseMs = currentPositionMsLocked();
    Mix_PauseMusic();
    m_playing = false;
    m_paused = true;
    m_playStartTick = 0;
    m_autoPausedForGame = true;
    persistLocked(true);
    switchu::FileLog::log("[music] auto-paused for blacklisted title=%016lX",
                          static_cast<unsigned long>(titleId));
}

void MusicService::onGameEnded() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    switchu::FileLog::log("[music-diag] GAME_ENDED autoPaused=%d session=%d current=%d",
                          m_autoPausedForGame ? 1 : 0, m_sessionActive ? 1 : 0, m_currentIndex);
    if (!m_autoPausedForGame)
        return;
    m_autoPausedForGame = false;
    if (!m_sessionActive)
        return;
    if (!m_music) {
        loadTrackLocked(m_currentIndex, true, m_positionBaseMs);
        return;
    }
    Mix_ResumeMusic();
    m_paused = false;
    m_playing = true;
    m_playStartTick = nowTick();
    persistLocked(true);
    switchu::FileLog::log("[music] resumed after blacklisted title closed");
}

} // namespace switchu::daemon::music
