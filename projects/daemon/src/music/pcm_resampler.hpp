#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace switchu::daemon::music {

// Lightweight streaming linear resampler. The music daemon owns all decoder
// and conversion state, so changing track only swaps a source; the SDL output
// device remains open. Output is always 48 kHz stereo signed-16 PCM.
class PcmResampler {
public:
    static constexpr int kOutputRate = 48000;
    static constexpr int kOutputChannels = 2;

    void configure(int sourceRate, int sourceChannels) {
        m_sourceRate = sourceRate;
        m_sourceChannels = sourceChannels;
        m_step = sourceRate > 0 ? static_cast<double>(sourceRate) / kOutputRate : 1.0;
        reset();
    }

    void reset() {
        m_pcm.clear();
        m_offsetFrames = 0;
        m_position = 0.0;
        m_finished = false;
    }

    void markFinished() { m_finished = true; }

    bool empty() const { return availableFrames() == 0; }

    void append(const std::int16_t* data, std::size_t frames) {
        if (!data || frames == 0 || m_sourceChannels <= 0) return;
        compactIfNeeded();
        const std::size_t samples = frames * static_cast<std::size_t>(m_sourceChannels);
        m_pcm.insert(m_pcm.end(), data, data + samples);
    }

    // Returns number of stereo output frames produced.
    std::size_t render(std::int16_t* outStereo, std::size_t maxFrames) {
        if (!outStereo || maxFrames == 0 || m_sourceRate <= 0 || m_sourceChannels <= 0)
            return 0;

        std::size_t produced = 0;
        while (produced < maxFrames) {
            const std::size_t frames = availableFrames();
            if (frames == 0) break;

            const std::size_t i0 = static_cast<std::size_t>(m_position);
            if (i0 >= frames) break;
            if (!m_finished && i0 + 1 >= frames) break; // wait for continuity sample

            const std::size_t i1 = std::min(i0 + 1, frames - 1);
            const double frac = m_position - static_cast<double>(i0);

            const auto s0 = stereoAt(i0);
            const auto s1 = stereoAt(i1);
            const double left = static_cast<double>(s0.first) +
                                (static_cast<double>(s1.first) - s0.first) * frac;
            const double right = static_cast<double>(s0.second) +
                                 (static_cast<double>(s1.second) - s0.second) * frac;
            outStereo[produced * 2 + 0] = clamp16(left);
            outStereo[produced * 2 + 1] = clamp16(right);
            ++produced;

            m_position += m_step;
            const std::size_t consumed = static_cast<std::size_t>(m_position);
            if (consumed > 0) {
                m_offsetFrames += consumed;
                m_position -= static_cast<double>(consumed);
                compactIfNeeded();
            }
        }
        return produced;
    }

private:
    std::size_t availableFrames() const {
        if (m_sourceChannels <= 0) return 0;
        const std::size_t total = m_pcm.size() / static_cast<std::size_t>(m_sourceChannels);
        return total > m_offsetFrames ? total - m_offsetFrames : 0;
    }

    std::pair<std::int16_t, std::int16_t> stereoAt(std::size_t relativeFrame) const {
        const std::size_t frame = m_offsetFrames + relativeFrame;
        const std::size_t base = frame * static_cast<std::size_t>(m_sourceChannels);
        if (m_sourceChannels == 1) {
            const auto s = m_pcm[base];
            return {s, s};
        }
        // For multichannel material, use the front L/R pair. Music files seen
        // by SwitchU are overwhelmingly mono/stereo, and this keeps conversion
        // deterministic without pulling in a second mixer/resampler library.
        return {m_pcm[base], m_pcm[base + 1]};
    }

    static std::int16_t clamp16(double value) {
        value = std::clamp(value, -32768.0, 32767.0);
        return static_cast<std::int16_t>(std::lrint(value));
    }

    void compactIfNeeded() {
        if (m_offsetFrames == 0 || m_sourceChannels <= 0) return;
        const std::size_t totalFrames = m_pcm.size() / static_cast<std::size_t>(m_sourceChannels);
        if (m_offsetFrames < 4096 && m_offsetFrames * 2 < totalFrames) return;

        const std::size_t sampleOffset = m_offsetFrames * static_cast<std::size_t>(m_sourceChannels);
        m_pcm.erase(m_pcm.begin(), m_pcm.begin() + static_cast<std::ptrdiff_t>(sampleOffset));
        m_offsetFrames = 0;
    }

    int m_sourceRate = 0;
    int m_sourceChannels = 0;
    double m_step = 1.0;
    double m_position = 0.0;
    bool m_finished = false;
    std::vector<std::int16_t> m_pcm;
    std::size_t m_offsetFrames = 0;
};

} // namespace switchu::daemon::music
