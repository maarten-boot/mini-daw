#pragma once

// Tracks and take file names (SPEC-REC-01, 01a, 07).

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
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
    // MIDI: a VST3 instrument (P10, SPEC-VST-02/06). Empty class = none. When
    // set, live and played MIDI go to it instead of `midiOut`.
    std::string instrumentClass;             // 32 hex digits
    std::string instrumentModule;            // UTF-8 path of the module (to find it again)
    std::string instrumentName;              // for messages when it is missing
    std::string instrumentState;             // the plugin's processor state (bytes)
    std::string instrumentControllerState;   // the plugin's controller state (bytes)
    std::string instrumentPreset;            // name of the preset or program chosen last (PL2), for display
    float volumeDb = 0.0f;                   // -60 (= silent) .. +6
    bool mute = false;
    bool solo = false;

    // The sound of an instrument (P11, SPEC-VST-03a). An audio track with
    // `instrumentOf` = N holds the recorded or rendered sound of MIDI track
    // N's instrument (its "linked" track); it has no input and is not armed
    // itself. MIDI track N's `soundFromRecording` chooses what plays: false =
    // the instrument plays the MIDI take (the linked track is silent), true =
    // the linked track's WAV plays (the instrument gets no take MIDI; live
    // playing on an armed track still sounds).
    int instrumentOf = 0;                    // audio: 0 = an input track
    bool soundFromRecording = false;         // MIDI with an instrument
};

inline constexpr float kMinVolumeDb = -60.0f;  // and below: silent
inline constexpr float kMaxVolumeDb = 6.0f;

// Linear gain of a track for playback: 0 when muted, or when another track is
// soloed and this one is not; else its volume (SPEC-PLAY-04).
float trackGain(const Track& track, bool anySolo);
bool anySolo(const std::vector<Track>& tracks);

// P11: the linked audio track of MIDI track `midiNumber` (see Track), or null.
const Track* linkedSoundTrack(const std::vector<Track>& tracks, int midiNumber);
Track* linkedSoundTrack(std::vector<Track>& tracks, int midiNumber);
// The MIDI track a linked audio track belongs to, or null.
const Track* soundOwner(const std::vector<Track>& tracks, const Track& linked);
// Playback gain with the "Sound from" rule (SPEC-VST-03a): trackGain, except
// that a linked audio track is silent unless its MIDI track takes its sound
// from the recording, and is also silent when that MIDI track is muted; the
// MIDI track's solo counts for its linked track too.
float playbackGain(const Track& track, const std::vector<Track>& tracks);
// Whether the MIDI track's instrument should get the take's MIDI in playback.
inline bool instrumentPlaysTake(const Track& midi) { return !midi.soundFromRecording; }
// How a new take of this track is placed on the timeline (SPEC-PLAY-05):
// with the audio latency estimate for audio inputs, with the MIDI one for MIDI
// tracks — and for a linked track too, because its sound comes from the same
// (late) MIDI notes as its MIDI take (crosscheck #10, R3).
inline bool placedAsAudioInput(const Track& t) { return t.kind == TrackKind::Audio && t.instrumentOf <= 0; }
// Removes tracks[index], keeping links consistent (crosscheck #10, R2): a
// removed MIDI track's linked track becomes a normal audio track; removing a
// linked track sets its MIDI track back to "Sound from: instrument".
void removeTrack(std::vector<Track>& tracks, std::size_t index);
// MIDI track `midiNumber`'s linked sound track (SPEC-VST-03a), made if
// missing: right below the MIDI track, named "<name> sound", silent
// ("Sound from" = instrument). Returns its number; 0 if `midiNumber` is not a
// MIDI track. May insert into `tracks` (references into it become invalid).
int addLinkedSoundTrack(std::vector<Track>& tracks, int midiNumber);
// Removes MIDI track `midiNumber`'s linked sound track if it holds no take
// (its instrument was taken away before anything was recorded or rendered).
// Returns true if one was removed.
bool removeUnusedLinkedTrack(std::vector<Track>& tracks, int midiNumber);
// How many tracks Record would play while the armed tracks record (an
// overdub, SPEC-PLAY-05a): not armed, with a take (a MIDI take only with an
// instrument or a MIDI output to play to). A linked sound track counts
// only when it plays its recording ("Sound from") and its MIDI track is not
// armed (an armed instrument track plays live).
int overdubTrackCount(const std::vector<Track>& tracks);
// The tracks as a recording should see them (R4, R9): MIDI tracks whose
// instrument is not loaded record no sound (their instrument class is
// cleared in the copy), and linked tracks are never armed themselves.
std::vector<Track> tracksToRecord(const std::vector<Track>& tracks, const std::function<bool(int)>& instrumentLoaded);

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

// A new file for rendering into `folder` for linked track `linked` (P11,
// crosscheck #10 R1): "NN-<name>-render.wav", or "-render-2.wav", "-3", ...
// when that exists, so a render never overwrites a take that may be playing.
std::filesystem::path newRenderPath(const std::filesystem::path& folder, const Track& linked);

// "take-YYYYMMDD-HHMMSS".
std::string takeFolderName(const std::tm& localTime);

// Creates base/name, or base/name-2, -3, ... if it already exists.
// Returns an empty path and sets `error` on failure.
std::filesystem::path createTakeFolder(const std::filesystem::path& base, const std::string& name,
                                       std::string& error);

// Lowest track number not used yet.
int nextTrackNumber(const std::vector<Track>& tracks);

} // namespace minidaw
