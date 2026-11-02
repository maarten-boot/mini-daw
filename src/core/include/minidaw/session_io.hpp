#pragma once

// What a RecordingSession needs from the audio stream and the MIDI inputs.
// AudioStream and MidiInputs implement these; the tests use stand-ins
// (crosscheck #4, J13).

#include "minidaw/frame_clock.hpp"
#include "minidaw/recorder.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace minidaw {

struct RawMidi;

class SessionAudio {
public:
    virtual ~SessionAudio() = default;
    virtual bool streamRunning() const = 0;
    virtual unsigned int streamSampleRate() const = 0;
    virtual unsigned int streamInputChannels() const = 0;
    virtual bool startRecording(const std::vector<TakeSpec>& takes, std::string& error,
                                const RecorderOptions& options) = 0;
    virtual RecordingResult stopRecording() = 0;
    virtual const Recorder& recorder() const = 0;
    // Audio timeline (SPEC-REC-04a); empty while no stream runs.
    virtual std::optional<std::uint64_t> frameAtTime(std::int64_t ns) const = 0;
    virtual std::optional<std::uint64_t> currentFrame() const = 0;
};

class SessionMidi {
public:
    virtual ~SessionMidi() = default;
    virtual bool isOpen(const std::string& port) const = 0;
    virtual void setCapture(bool on) = 0;
    virtual std::size_t read(const std::string& port, RawMidi* out, std::size_t max) = 0;
    virtual std::uint64_t messagesLost(const std::string& port) const = 0;
};

} // namespace minidaw
