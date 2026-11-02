#pragma once

// One recording: all armed tracks, started and stopped together
// (SPEC-REC-01..04, 04a-c, 07).
//
// start() creates the take folder, starts the audio Recorder for the armed
// audio tracks and, for armed MIDI tracks, a thread that empties the MIDI
// rings, places each message on the audio timeline and rewrites each MIDI
// file about once a second. stop() ends everything and writes the final
// files. The audio stream must keep running from start() to stop().

#include "minidaw/audio_stream.hpp"
#include "minidaw/config.hpp"
#include "minidaw/midi_input.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/track.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

struct MidiTakeResult {
    std::filesystem::path path;
    std::string port;
    std::size_t events = 0;           // events in the file (incl. added note-offs)
    std::uint64_t lostMessages = 0;   // the port's ring was full
};

struct SessionResult {
    std::filesystem::path folder;
    RecordingResult audio;
    std::vector<MidiTakeResult> midi;
    double seconds = 0.0;
    std::string error;                // first MIDI file error, empty if none
    bool lostData() const;
};

class RecordingSession {
public:
    RecordingSession() = default;
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    // Records all armed tracks. Needs a running stream; MIDI tracks need their
    // port open in `midi`. On failure nothing is recorded and the new take
    // folder is removed again.
    bool start(const std::vector<Track>& tracks, const RecordingSettings& settings, AudioStream& audio,
               MidiInputs& midi, std::string& error);
    SessionResult stop();

    bool recording() const { return active_; }
    const std::filesystem::path& folder() const { return folder_; }

    // Progress (same thread as start/stop).
    double elapsedSeconds() const;
    std::uint64_t lostFrames() const;
    std::string diskError() const;
    std::size_t midiMessages() const { return midiMessages_.load(std::memory_order_relaxed); }

private:
    struct MidiTake {
        Track track;
        std::filesystem::path path;
    };
    struct PortEvents {
        std::string port;
        std::vector<TimedMidi> events;
    };

    void midiLoop();
    void collectMidi();                                   // rings -> portEvents_
    std::optional<std::uint64_t> timelineStart() const;
    bool writeMidiFiles(std::uint64_t endFrame, bool final, std::vector<MidiTakeResult>* results);
    void setError(const std::string& text);

    bool active_ = false;
    bool hasAudio_ = false;
    AudioStream* audio_ = nullptr;
    MidiInputs* midi_ = nullptr;
    RecordingSettings settings_;
    unsigned int sampleRate_ = 0;
    std::filesystem::path folder_;
    std::string takeName_;
    std::uint64_t midiOnlyStart_ = 0;                     // used when no audio track is armed

    std::vector<MidiTake> midiTakes_;
    std::vector<PortEvents> portEvents_;                  // MIDI thread (and stop() after join)
    std::atomic<std::size_t> midiMessages_{0};
    std::atomic<bool> midiRun_{false};
    std::thread midiThread_;
    mutable std::mutex errorMutex_;
    std::string error_;
};

} // namespace minidaw
