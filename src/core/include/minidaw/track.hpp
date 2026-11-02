#pragma once

// Tracks and take file names (SPEC-REC-01, 01a, 07).

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

enum class TrackKind { Audio, Midi };

struct Track {
    int number = 1;                          // shown as 1, 2, ...; also the file name prefix
    std::string name;
    TrackKind kind = TrackKind::Audio;
    std::vector<unsigned int> channels;      // audio: input channels (0-based), 1 or 2
    std::string midiPort;                    // MIDI: input port name
    int midiChannel = 0;                     // MIDI: 0 = all, 1..16
    bool armed = false;
    bool monitor = false;                    // audio: input monitoring while armed (SPEC-MON-01)

    // Playback (P8, SPEC-PLAY-02..04).
    std::filesystem::path take;              // the take it plays (last recorded or loaded); empty = none
    std::int64_t takeStart = 0;              // timeline frame of the take's first frame (overdub shift: < 0 ok)
    std::uint64_t takeFrames = 0;            // its length at the session rate (takeLengthFrames)
    std::string midiOut;                     // MIDI: output port for playback; empty = none (default)
    float volumeDb = 0.0f;                   // -60 (= silent) .. +6
    bool mute = false;
    bool solo = false;
};

inline constexpr float kMinVolumeDb = -60.0f;  // and below: silent
inline constexpr float kMaxVolumeDb = 6.0f;

// Linear gain of a track for playback: 0 when muted, or when another track is
// soloed and this one is not; else its volume (SPEC-PLAY-04).
float trackGain(const Track& track, bool anySolo);
bool anySolo(const std::vector<Track>& tracks);

// Length of the track's take in frames at `sampleRate` (WAV: its frames;
// MIDI: its end of track); 0 if there is none or it cannot be read.
std::uint64_t takeLengthFrames(const Track& track, unsigned int sampleRate);

// Take folders ("take-*") in `base`, newest first (their names sort by time).
std::vector<std::filesystem::path> listTakeFolders(const std::filesystem::path& base);

// Tracks for the files of a take folder (SPEC-PLAY-02, P8): "NN-name.wav" ->
// audio track (input 1, or 1+2 for a stereo file), "NN-name.mid" -> MIDI
// track; numbered and named after the file, ordered by NN; each plays its
// file from timeline frame 0. Other files are ignored. Empty with `error`
// when nothing playable is found.
std::vector<Track> tracksFromTakeFolder(const std::filesystem::path& folder, std::string& error);

// One audio track on inputs 1+2 (or 1 if there is only one input); none without inputs.
std::vector<Track> defaultTracks(unsigned int inputChannels);

// Characters not allowed in Windows/Linux file names (and control characters)
// become '_'; leading/trailing spaces and dots are removed; empty -> "track".
std::string safeFileName(std::string_view name);

// "NN-<safe name>.wav" / ".mid".
std::string takeFileName(const Track& track);

// "take-YYYYMMDD-HHMMSS".
std::string takeFolderName(const std::tm& localTime);

// Creates base/name, or base/name-2, -3, ... if it already exists.
// Returns an empty path and sets `error` on failure.
std::filesystem::path createTakeFolder(const std::filesystem::path& base, const std::string& name,
                                       std::string& error);

// Lowest track number not used yet.
int nextTrackNumber(const std::vector<Track>& tracks);

} // namespace minidaw
