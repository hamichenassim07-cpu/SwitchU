#include "stream_decoder.hpp"

#include <FLAC/stream_decoder.h>
#include <mpg123.h>
#include <switchu/file_log.hpp>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace switchu::daemon::music {
namespace {

bool ensureMpg123Initialized() {
    static const bool ready = (mpg123_init() == MPG123_OK);
    return ready;
}

std::string lowerExtension(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

class Mpg123Decoder final : public StreamDecoder {
public:
    explicit Mpg123Decoder(const std::string& path) {
        // FIX6: Keep the libmpg123 handle local until the whole compressed
        // stream setup has succeeded. FIX5 hardware logs showed m_handle
        // becoming null between getformat() and the old format_none()/format()
        // sequence. Committing the handle only at the end prevents a partially
        // constructed decoder from exposing a damaged member state.
        const std::string stablePath = path;
        switchu::FileLog::log("[music-decoder] mpg123 FIX6 ctor begin path=%s", stablePath.c_str());
        if (!ensureMpg123Initialized()) {
            m_failed = true;
            m_error = "mpg123_init failed";
            return;
        }

        int err = MPG123_OK;
        mpg123_handle* handle = nullptr;

        auto failCtor = [&](const char* where, int rc) {
            m_failed = true;
            const char* detail = mpg123_plain_strerror(rc);
            m_error = std::string(where) + ": " + (detail ? detail : "unknown");
            switchu::FileLog::log("[music-decoder] mpg123 FIX6 ctor FAIL where=%s rc=%d detail=%s handle=%p",
                                  where, rc, detail ? detail : "unknown", static_cast<void*>(handle));
            if (handle) {
                mpg123_delete(handle); // delete() also closes an opened source.
                handle = nullptr;
            }
        };

        switchu::FileLog::log("[music-decoder] mpg123_new begin");
        handle = mpg123_new(nullptr, &err);
        switchu::FileLog::log("[music-decoder] mpg123_new done local=%p err=%d", static_cast<void*>(handle), err);
        if (!handle) {
            failCtor("mpg123_new", err);
            return;
        }

        // devkitPro currently ships switch-mpg123 1.31.3. Older libmpg123
        // releases are vulnerable to the 'Frankenstein stream' format-change
        // corruption fixed upstream in 1.32.8. The daemon only needs raw audio
        // (metadata is parsed by SwitchU's library), so disable ID3v2 parsing
        // and explicitly reject changing/concatenated MPEG stream formats.
        const long safeFlags = static_cast<long>(MPG123_NO_FRANKENSTEIN) |
                               static_cast<long>(MPG123_SKIP_ID3V2);
        const int flagRc = mpg123_param(handle, MPG123_ADD_FLAGS, safeFlags, 0.0);
        switchu::FileLog::log("[music-decoder] mpg123 FIX6 flags rc=%d flags=0x%lX local=%p",
                              flagRc, safeFlags, static_cast<void*>(handle));
        if (flagRc != MPG123_OK) {
            failCtor("mpg123_param", flagRc);
            return;
        }

        // Use libmpg123's dedicated fixed-output path instead of configuring
        // format_none()/format() after the stream has already been parsed.
        // SwitchU's AudioOut is stereo signed-16; mono MP3s are duplicated by
        // libmpg123. The source sample rate remains native and is resampled by
        // SwitchU's own PcmResampler.
        switchu::FileLog::log("[music-decoder] mpg123_open_fixed begin local=%p path=%s",
                              static_cast<void*>(handle), stablePath.c_str());
        const int openRc = mpg123_open_fixed(handle, stablePath.c_str(),
                                             MPG123_STEREO, MPG123_ENC_SIGNED_16);
        switchu::FileLog::log("[music-decoder] mpg123_open_fixed done rc=%d local=%p",
                              openRc, static_cast<void*>(handle));
        if (openRc != MPG123_OK) {
            failCtor("mpg123_open_fixed", openRc);
            return;
        }

        long rate = 0;
        int channels = 0;
        int encoding = 0;
        switchu::FileLog::log("[music-decoder] mpg123_getformat begin local=%p", static_cast<void*>(handle));
        const int formatRc = mpg123_getformat(handle, &rate, &channels, &encoding);
        switchu::FileLog::log(
            "[music-decoder] mpg123_getformat done rc=%d local=%p rate=%ld ch=%d enc=0x%X",
            formatRc, static_cast<void*>(handle), rate, channels, static_cast<unsigned int>(encoding));
        if (formatRc != MPG123_OK || rate <= 0 || channels != 2 ||
            encoding != MPG123_ENC_SIGNED_16) {
            failCtor("mpg123_getformat/fixed-format", formatRc != MPG123_OK ? formatRc : MPG123_BAD_OUTFORMAT);
            return;
        }

        // Commit only after every setup call succeeded. There is intentionally
        // no mpg123_format_none()/mpg123_format() call after this point.
        m_handle = handle;
        handle = nullptr;
        m_rate = static_cast<int>(rate);
        m_channels = channels;
        switchu::FileLog::log(
            "[music-decoder] mpg123 FIX6 ctor ready member=%p rate=%d ch=%d",
            static_cast<void*>(m_handle), m_rate, m_channels);
    }

    ~Mpg123Decoder() override {
        if (m_handle) {
            switchu::FileLog::log("[music-decoder] mpg123 dtor close begin handle=%p", static_cast<void*>(m_handle));
            mpg123_close(m_handle);
            switchu::FileLog::log("[music-decoder] mpg123 dtor close done handle=%p", static_cast<void*>(m_handle));
            mpg123_delete(m_handle);
            m_handle = nullptr;
            switchu::FileLog::log("[music-decoder] mpg123 dtor delete done");
        }
    }

    bool valid() const { return m_handle && !m_failed && m_rate > 0 && m_channels == 2; }

    int sampleRate() const override { return m_rate; }
    int channels() const override { return m_channels; }

    std::size_t readFrames(std::int16_t* out, std::size_t frames) override {
        if (!valid() || !out || frames == 0 || m_eof) return 0;
        const std::size_t bytesWanted = frames * static_cast<std::size_t>(m_channels) * sizeof(std::int16_t);
        std::size_t bytesDone = 0;
        mpg123_handle* const handle = m_handle;
        const int rc = mpg123_read(handle,
                                   reinterpret_cast<unsigned char*>(out),
                                   bytesWanted,
                                   &bytesDone);
        if (rc == MPG123_DONE) {
            m_eof = true;
        } else if (rc == MPG123_NEW_FORMAT) {
            // open_fixed()+NO_FRANKENSTEIN promises one stable output format.
            // Do not attempt a late renegotiation on the vulnerable 1.31.x
            // backend; fail cleanly instead of risking heap corruption.
            failCode("mpg123_read unexpected format change", rc);
        } else if (rc != MPG123_OK) {
            failCode("mpg123_read", rc);
        }
        return bytesDone / (static_cast<std::size_t>(m_channels) * sizeof(std::int16_t));
    }

    bool seekMs(std::uint64_t positionMs) override {
        if (!valid()) return false;
        const off_t frame = static_cast<off_t>((positionMs * static_cast<std::uint64_t>(m_rate)) / 1000ULL);
        if (mpg123_seek(m_handle, frame, SEEK_SET) < 0) {
            m_failed = true;
            m_error = "mpg123_seek failed";
            return false;
        }
        m_eof = false;
        return true;
    }

    bool eof() const override { return m_eof; }
    bool failed() const override { return m_failed; }
    const std::string& error() const override { return m_error; }

private:
    void failCode(const char* where, int rc) {
        m_failed = true;
        const char* detail = mpg123_plain_strerror(rc);
        m_error = std::string(where) + ": " + (detail ? detail : "unknown");
    }

    mpg123_handle* m_handle = nullptr;
    int m_rate = 0;
    int m_channels = 0;
    bool m_eof = false;
    bool m_failed = false;
    std::string m_error;
};

class FlacDecoder final : public StreamDecoder {
public:
    explicit FlacDecoder(const std::string& path) {
        m_decoder = FLAC__stream_decoder_new();
        if (!m_decoder) {
            m_failed = true;
            m_error = "FLAC__stream_decoder_new failed";
            return;
        }

        FLAC__stream_decoder_set_md5_checking(m_decoder, false);
        const auto status = FLAC__stream_decoder_init_file(
            m_decoder, path.c_str(), &FlacDecoder::writeCallback,
            &FlacDecoder::metadataCallback, &FlacDecoder::errorCallback, this);
        if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            m_failed = true;
            m_error = std::string("FLAC init: ") + FLAC__StreamDecoderInitStatusString[status];
            return;
        }
        m_initialized = true;

        if (!FLAC__stream_decoder_process_until_end_of_metadata(m_decoder) ||
            m_rate <= 0 || m_channels <= 0) {
            m_failed = true;
            if (m_error.empty()) m_error = "FLAC metadata decode failed";
        }
    }

    ~FlacDecoder() override {
        if (m_decoder) {
            if (m_initialized) FLAC__stream_decoder_finish(m_decoder);
            FLAC__stream_decoder_delete(m_decoder);
        }
    }

    bool valid() const { return m_decoder && m_initialized && !m_failed && m_rate > 0 && m_channels > 0; }

    int sampleRate() const override { return m_rate; }
    int channels() const override { return m_channels; }

    std::size_t readFrames(std::int16_t* out, std::size_t frames) override {
        if (!valid() || !out || frames == 0) return 0;
        std::size_t produced = 0;

        while (produced < frames) {
            const std::size_t availableSamples = m_pending.size() - m_pendingOffset;
            const std::size_t availableFrames = m_channels > 0
                ? availableSamples / static_cast<std::size_t>(m_channels) : 0;
            if (availableFrames > 0) {
                const std::size_t take = std::min(frames - produced, availableFrames);
                const std::size_t samples = take * static_cast<std::size_t>(m_channels);
                std::memcpy(out + produced * static_cast<std::size_t>(m_channels),
                            m_pending.data() + m_pendingOffset,
                            samples * sizeof(std::int16_t));
                m_pendingOffset += samples;
                produced += take;
                if (m_pendingOffset == m_pending.size()) {
                    m_pending.clear();
                    m_pendingOffset = 0;
                }
                continue;
            }

            if (m_eof || m_failed) break;
            if (!FLAC__stream_decoder_process_single(m_decoder)) {
                m_failed = true;
                if (m_error.empty()) m_error = "FLAC process_single failed";
                break;
            }
            if (FLAC__stream_decoder_get_state(m_decoder) == FLAC__STREAM_DECODER_END_OF_STREAM)
                m_eof = true;
        }

        return produced;
    }

    bool seekMs(std::uint64_t positionMs) override {
        if (!valid()) return false;
        const FLAC__uint64 sample = (positionMs * static_cast<std::uint64_t>(m_rate)) / 1000ULL;
        m_pending.clear();
        m_pendingOffset = 0;
        m_eof = false;
        if (!FLAC__stream_decoder_seek_absolute(m_decoder, sample)) {
            m_failed = true;
            m_error = "FLAC seek failed";
            return false;
        }
        return true;
    }

    bool eof() const override { return m_eof; }
    bool failed() const override { return m_failed; }
    const std::string& error() const override { return m_error; }

private:
    static FLAC__StreamDecoderWriteStatus writeCallback(
        const FLAC__StreamDecoder*, const FLAC__Frame* frame,
        const FLAC__int32* const buffer[], void* clientData) {
        auto* self = static_cast<FlacDecoder*>(clientData);
        if (!self || !frame || !buffer || self->m_channels <= 0)
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

        const std::size_t block = frame->header.blocksize;
        const std::size_t channels = static_cast<std::size_t>(self->m_channels);
        const std::size_t oldSize = self->m_pending.size();
        self->m_pending.resize(oldSize + block * channels);

        const int bits = self->m_bitsPerSample > 0 ? self->m_bitsPerSample : 16;
        for (std::size_t i = 0; i < block; ++i) {
            for (std::size_t c = 0; c < channels; ++c) {
                std::int64_t sample = buffer[c][i];
                if (bits > 16) sample >>= (bits - 16);
                else if (bits < 16) sample <<= (16 - bits);
                sample = std::clamp<std::int64_t>(sample, INT16_MIN, INT16_MAX);
                self->m_pending[oldSize + i * channels + c] = static_cast<std::int16_t>(sample);
            }
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    static void metadataCallback(const FLAC__StreamDecoder*,
                                 const FLAC__StreamMetadata* metadata,
                                 void* clientData) {
        auto* self = static_cast<FlacDecoder*>(clientData);
        if (!self || !metadata || metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;
        self->m_rate = static_cast<int>(metadata->data.stream_info.sample_rate);
        self->m_channels = static_cast<int>(metadata->data.stream_info.channels);
        self->m_bitsPerSample = static_cast<int>(metadata->data.stream_info.bits_per_sample);
    }

    static void errorCallback(const FLAC__StreamDecoder*,
                              FLAC__StreamDecoderErrorStatus status,
                              void* clientData) {
        auto* self = static_cast<FlacDecoder*>(clientData);
        if (!self) return;
        self->m_failed = true;
        self->m_error = std::string("FLAC decoder: ") + FLAC__StreamDecoderErrorStatusString[status];
    }

    FLAC__StreamDecoder* m_decoder = nullptr;
    bool m_initialized = false;
    int m_rate = 0;
    int m_channels = 0;
    int m_bitsPerSample = 16;
    bool m_eof = false;
    bool m_failed = false;
    std::string m_error;
    std::vector<std::int16_t> m_pending;
    std::size_t m_pendingOffset = 0;
};

} // namespace

std::unique_ptr<StreamDecoder> openStreamDecoder(const std::string& path,
                                                 std::string& errorOut) {
    errorOut.clear();
    const std::string ext = lowerExtension(path);

    if (ext == ".mp3") {
        auto decoder = std::make_unique<Mpg123Decoder>(path);
        if (!decoder->valid()) {
            errorOut = decoder->error();
            return nullptr;
        }
        return decoder;
    }

    if (ext == ".flac") {
        auto decoder = std::make_unique<FlacDecoder>(path);
        if (!decoder->valid()) {
            errorOut = decoder->error();
            return nullptr;
        }
        return decoder;
    }

    errorOut = "unsupported audio format: " + ext;
    return nullptr;
}

} // namespace switchu::daemon::music
