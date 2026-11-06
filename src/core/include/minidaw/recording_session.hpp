#pragma once

// One recording: all armed tracks, started and stopped together
// (SPEC-REC-01..04, 04a-c, 07).
//
// start() creates the take folder, starts the audio Recorder for the armed
// audio tracks and, for armed MIDI tracks, a thread that empties the MIDI
// rings, places each message on the audio timeline and rewrites each MIDI
// file about once a second. stop() ends everything and writes the final
// files. The audio stream must keep running from start() to stop().
//
// startAuto() (auto record, P16, SPEC-REC-08) records the same tracks in
// parts: autoUpdate(), called by the GUI thread every frame, starts a part
// when someone plays and ends it after a pause; each part is its own folder
// "<takes>/auto/<date time>" with the usual file names. Nothing is written
// between parts.

#include "minidaw/auto_record.hpp"
#include "minidaw/config.hpp"
#include "minidaw/midi_input.hpp"
#include "minidaw/session_io.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/track.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

struct MidiTakeResult {
    std::filesystem::path path;
    std::string port;
    std::size_t events = 0;           // events in the file (incl. added note-offs)
    std::uint64_t lostMessages = 0;   // the port's ring was full, or the message could not be placed
    std::uint64_t disconnects = 0;    // the device went away during the take (SPEC-REC-01b): MIDI is missing
};

struct SessionResult {
    std::filesystem::path folder;
    RecordingResult audio;
    std::vector<MidiTakeResult> midi;
    double seconds = 0.0;
    std::optional<std::uint64_t> startFrame;  // stream frame of the takes' frame 0 (for the timeline, P8)
    std::string error;                // first MIDI file error, empty if none
    bool lostData() const;
};

// Auto record: the earliest MIDI event still needed: the GUI's `keepFrom`,
// but never after the start of the open part or of a part still to be
// written (crosscheck #11, S1).
std::uint64_t midiKeepFrom(std::uint64_t keepFrom, std::optional<std::uint64_t> openFrom,
                           const std::vector<std::uint64_t>& pendingFroms);

// A part of an auto recording (P16).
struct AutoPartEvent {
    enum class Kind { None, Began, Ended, Failed } kind = Kind::None;
    std::filesystem::path folder;
    double seconds = 0.0;   // Ended: its length
    double cutSeconds = 0.0;  // Began: playing before this part's start that could not be kept (S2)
    std::string error;      // Failed: why no folder could be made
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
    bool start(const std::vector<Track>& tracks, const RecordingSettings& settings, SessionAudio& audio,
               SessionMidi& midi, std::string& error);
    SessionResult stop();

    // Auto record: like start(), but in parts (see above). stop() ends a part
    // that is open.
    bool startAuto(const std::vector<Track>& tracks, const RecordingSettings& settings, SessionAudio& audio,
                   SessionMidi& midi, std::string& error);
    // GUI thread, every frame while auto recording.
    AutoPartEvent autoUpdate();
    bool autoMode() const { return active_ && auto_; }
    bool autoInPart() const { return autoMode() && tracker_.inPart(); }
    double autoPartSeconds() const;   // length of the open part so far
    double autoQuietSeconds() const;  // time since the last playing
    int autoPartsDone() const { return partsDone_; }
    // Audio lost in parts, and files that failed (from the recorder).
    std::uint64_t autoLostFrames() const { return partLost_; }

    bool recording() const { return active_; }
    const std::filesystem::path& folder() const { return folder_; }

    // Progress (same thread as start/stop).
    double elapsedSeconds() const;
    std::uint64_t lostFrames() const;
    std::string diskError() const;
    std::size_t midiMessages() const { return midiMessages_.load(std::memory_order_relaxed); }
    std::uint64_t midiLost() const;   // lost MIDI messages so far (SPEC-REC-05b, J11)

private:
    struct MidiTake {
        Track track;
        std::filesystem::path path;
        std::uint64_t disconnectsAtStart = 0;
    };
    // Notes down and sustain pedals per channel (auto record: a held note keeps a part open).
    struct PortEventsHeld {
        std::vector<std::uint8_t> down = std::vector<std::uint8_t>(16 * 128, 0);
        bool sustain[16] = {};
        int count = 0;
        std::uint16_t channels = 0;          // bit per MIDI channel an armed track records (S8)
        std::uint64_t disconnectsSeen = 0;   // a disconnect releases the port's notes (S6)
    };
    struct PortEvents {
        std::string port;
        std::vector<TimedMidi> events;   // kept sorted by frame
        std::uint64_t unplaced = 0;      // messages that could not be put on the timeline
        PortEventsHeld held;
    };

    bool begin(const std::vector<Track>& tracks, const RecordingSettings& settings, SessionAudio& audio,
               SessionMidi& midi, std::string& error, bool autoMode);
    void midiLoop();
    // Auto record.
    struct MidiPart {
        std::filesystem::path folder;
        std::uint64_t from = 0;
        std::uint64_t to = 0;
    };
    void noteActivity(PortEventsHeld& held, const TimedMidi& m);
    void releaseHeld(PortEventsHeld& held, int channel);  // -1: all channels
    std::uint64_t lastStartActivity() const;
    void writeMidiPart(const MidiPart& part, bool final);
    void runMidiPartJobs(bool final);
    AutoPartEvent endAutoPart(const AutoPartTracker::Action& a);
    std::uint64_t lastActivity() const;
    void collectPartResults();
    void collectMidi();                                   // rings -> portEvents_
    std::optional<std::uint64_t> timelineStart() const;
    bool writeMidiFiles(std::uint64_t endFrame, bool final, std::vector<MidiTakeResult>* results);
    void setError(const std::string& text);

    bool active_ = false;
    bool hasAudio_ = false;
    SessionAudio* audio_ = nullptr;
    SessionMidi* midi_ = nullptr;
    RecordingSettings settings_;
    unsigned int sampleRate_ = 0;
    std::filesystem::path folder_;
    std::string takeName_;
    std::uint64_t midiOnlyStart_ = 0;                     // used when no audio track is armed

    std::vector<MidiTake> midiTakes_;
    std::vector<PortEvents> portEvents_;                  // MIDI thread (and stop() after join)
    std::atomic<std::size_t> midiMessages_{0};
    std::atomic<std::uint64_t> midiUnplaced_{0};
    std::atomic<bool> midiRun_{false};
    std::thread midiThread_;
    mutable std::mutex errorMutex_;
    std::string error_;               // final errors (kept)
    std::string midiRetryError_;      // a periodic MIDI write failed; cleared when one succeeds (J10)

    // Auto record. GUI thread:
    bool auto_ = false;
    AutoPartTracker tracker_;
    std::filesystem::path autoBase_;
    std::vector<std::filesystem::path> audioFileNames_;  // per audio take, in start order
    std::filesystem::path partFolder_;
    std::uint64_t partFrom_ = 0;
    int partId_ = 0;
    int partsDone_ = 0;
    std::uint64_t partLost_ = 0;
    // Shared with the MIDI thread:
    std::mutex partMutex_;
    bool midiPartOpen_ = false;          // under partMutex_
    MidiPart midiOpenPart_;              // under partMutex_
    std::vector<MidiPart> midiJobs_;     // ended parts to write, under partMutex_
    std::atomic<std::uint64_t> keepFrom_{0};          // MIDI events before this are not needed
    std::atomic<std::uint64_t> midiLastActivity_{0};  // stream frame of the last channel message
    std::atomic<int> midiHeld_{0};                    // notes down and sustain pedals
    std::atomic<std::uint64_t> midiEarliest_{UINT64_MAX};  // earliest sound-starting message since the GUI looked
    std::atomic<std::uint64_t> midiLastStart_{0};          // the last sound-starting message
};

} // namespace minidaw
