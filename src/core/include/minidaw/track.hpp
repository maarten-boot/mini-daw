#pragma once

// Tracks and take file names (SPEC-REC-01, 01a, 07).

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
};

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
