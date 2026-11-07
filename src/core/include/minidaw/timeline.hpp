#pragma once

// Timeline of recordings (P17, SPEC-PLAY-07): the take folders ("take-…") and
// auto record parts ("auto/<date time>") in the takes folder, in time order,
// with long gaps between them drawn short; loading one onto the tracks.

#include "minidaw/track.hpp"

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace minidaw {

struct RecordingInfo {
    std::filesystem::path folder;
    bool autoPart = false;                   // an auto record part (else a Record take)
    std::int64_t start = 0;                  // local time from the folder name, as seconds (see recordingTime)
    double seconds = 0.0;                    // the longest file in it
    std::vector<std::filesystem::path> files;  // "NN-name.wav" / ".mid", by name
};

// Start time from a folder name: "take-YYYYMMDD-HHMMSS" or (auto part)
// "YYYY-MM-DD HH-MM-SS", either with an optional "-N" suffix. Seconds of the
// local time as if it were UTC (only differences and the clock time are used).
bool recordingTime(const std::string& folderName, bool autoPart, std::int64_t& seconds);

// The recordings in a takes folder (and its "auto" folder), oldest first.
// Folders whose name has no time are left out.
std::vector<RecordingInfo> listRecordings(const std::filesystem::path& base);

struct TimelineBlock {
    std::size_t index = 0;  // into the recordings given
    double x = 0.0;
    double width = 0.0;
};
struct TimelineGap {
    double x = 0.0;
    double width = 0.0;
    std::string label;      // "+12 min"
};
struct TimelineLayout {
    std::vector<TimelineBlock> blocks;
    std::vector<TimelineGap> gaps;
    double width = 0.0;     // the whole timeline
};

// Recordings side by side in time order (they must be sorted by start):
// `pixelsPerSecond` for the recordings and for gaps up to `maxRealGap`
// seconds; a longer gap becomes a marker of `gapWidth` pixels labelled with
// its real length. Blocks are at least `minWidth` wide.
TimelineLayout layoutTimeline(const std::vector<RecordingInfo>& recordings, double pixelsPerSecond,
                              double maxRealGap, double gapWidth, double minWidth);

// "+45 s", "+12 min", "+2 h 05 min", "+3 days".
std::string gapLabel(double seconds);
// "21:51" (with the date "2026-11-06 21:51" when `withDate`).
std::string clockLabel(std::int64_t seconds, bool withDate);

// Puts a recording's files on the tracks (P17): "NN-name.ext" goes to track
// NN if it is of the same kind (.wav audio, .mid MIDI), as its take from
// frame 0; the tracks keep their instruments and settings. Files without such
// a track get new tracks (numbered after the others). Returns the number of
// files placed; 0 with `error` if there were none.
int assignTakeFolder(std::vector<Track>& tracks, const std::filesystem::path& folder, std::string& error);

} // namespace minidaw
