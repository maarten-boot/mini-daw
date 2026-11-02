#pragma once

// Standard MIDI Files (SPEC-REC-04, 04b, 04c).
//
// Events carry their position as an audio frame on the take's timeline
// (frame 0 = first frame of the audio takes). writeSmf() converts frames to
// ticks: 960 PPQ at a fixed 120 BPM (1920 ticks per second), or SMPTE
// 25 fps x 40 sub-frames (1000 ticks per second).
//
// buildMidiTake() turns the raw messages of one input port into the events of
// one MIDI track: channel filter, start/end of the take, note-offs for notes
// still held at the end.
//
// readSmf() reads a file; smfToFrames() turns it into timeline events for playback (P8).

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace minidaw {

enum class MidiTiming { Ppq, Smpte };

inline constexpr std::uint16_t kSmfPpq = 960;
inline constexpr std::uint32_t kSmfTempoUsPerQuarter = 500000;  // 120 BPM
inline constexpr int kSmfSmpteFps = 25;
inline constexpr int kSmfSmpteSubframes = 40;                   // 25 x 40 = 1000 ticks/s

// "ppq" / "smpte" (any case).
bool parseMidiTiming(const std::string& text, MidiTiming& out);
const char* midiTimingName(MidiTiming timing);

struct MidiEvent {
    std::uint64_t frame = 0;                 // position on the take's timeline
    std::array<std::uint8_t, 3> data{};      // channel message: status + up to 2 data bytes
    std::uint8_t size = 0;
};

struct SmfTrack {
    std::string name;
    std::vector<MidiEvent> events;           // sorted by frame (stable for equal frames)
};

std::uint32_t framesToTicks(std::uint64_t frame, unsigned int sampleRate, MidiTiming timing);

// Writes a type 1 file: track 0 = take name (+ tempo for PPQ), then one track
// per entry of `tracks`. `endFrame` places the end-of-track events. The file is
// written to "<path>.tmp" and renamed, so an existing file is replaced in one
// step; with `syncToDisk` it is forced to disk before the rename.
bool writeSmf(const std::filesystem::path& path, const std::string& takeName, const std::vector<SmfTrack>& tracks,
              unsigned int sampleRate, MidiTiming timing, std::uint64_t endFrame, bool syncToDisk,
              std::string& error);

// One message from a MIDI input, already placed on the absolute audio timeline.
struct TimedMidi {
    std::uint64_t frame = 0;                 // absolute stream frame
    std::array<std::uint8_t, 3> data{};
    std::uint8_t size = 0;
};

// channel: 0 = all, 1..16 = only that channel. Keeps channel messages with
// startFrame <= frame < endFrame, makes them relative to startFrame, and adds
// note-offs at (endFrame - startFrame) for notes still on.
std::vector<MidiEvent> buildMidiTake(const std::vector<TimedMidi>& input, int channel, std::uint64_t startFrame,
                                     std::uint64_t endFrame);

// Is this a channel message mini-daw records (SPEC-REC-04b)? Returns its length, or 0.
std::uint8_t channelMessageSize(std::uint8_t status);

// --- Reader (tests, playback later) ----------------------------------------
struct SmfReadEvent {
    std::uint32_t tick = 0;                  // absolute
    bool meta = false;
    std::uint8_t metaType = 0;
    std::vector<std::uint8_t> bytes;         // channel message, or meta payload
};

struct SmfData {
    std::uint16_t format = 0;
    std::int16_t division = 0;               // > 0: PPQ; < 0: SMPTE (high byte -fps, low byte sub-frames)
    std::vector<std::vector<SmfReadEvent>> tracks;
};

bool readSmf(const std::filesystem::path& path, SmfData& data, std::string& error);

// Playback (SPEC-PLAY-03): all channel messages of all tracks, placed on the
// audio timeline at `sampleRate` (frame 0 = tick 0), sorted (stable: the
// order in the file is kept for equal times). Follows tempo changes (any
// track) for PPQ files; SMPTE files use their fixed ticks per second
// (-29 = 29.97 fps). `endFrame` gets the latest end-of-track or event.
std::vector<MidiEvent> smfToFrames(const SmfData& data, unsigned int sampleRate, std::uint64_t* endFrame = nullptr);

} // namespace minidaw
