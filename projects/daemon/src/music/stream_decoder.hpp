#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace switchu::daemon::music {

// Small decoder abstraction owned entirely by the SwitchU music daemon.
// It deliberately exposes raw signed-16 PCM instead of any playback object:
// the output device can therefore stay alive while sources are replaced.
class StreamDecoder {
public:
    virtual ~StreamDecoder() = default;

    virtual int sampleRate() const = 0;
    virtual int channels() const = 0;

    // Decode up to `frames` native-rate PCM frames into `outInterleaved`.
    // Returns the number of frames produced. 0 means EOF or a decoder error;
    // check failed() to distinguish the two.
    virtual std::size_t readFrames(std::int16_t* outInterleaved, std::size_t frames) = 0;

    virtual bool seekMs(std::uint64_t positionMs) = 0;
    virtual bool eof() const = 0;
    virtual bool failed() const = 0;
    virtual const std::string& error() const = 0;
};

std::unique_ptr<StreamDecoder> openStreamDecoder(const std::string& path,
                                                 std::string& errorOut);

} // namespace switchu::daemon::music
