#pragma once

// Timeline of recordings (P17, SPEC-PLAY-07): the take folders ("take-…") and
// auto record parts ("auto/<date time>") in the takes folder, in time order,
// with long gaps between them drawn short; loading one onto the tracks.

#include "minidaw/recording_notes.hpp"
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
    std::int64_t start = 0;                  // when it started, in real seconds: order and pauses (continuousTime)
    std::int64_t clock = 0;                  // the local clock time in its name (recordingTime): the labels
    double seconds = 0.0;                    // the longest file in it
    std::vector<std::filesystem::path> files;  // "NN-name.wav" / ".flac" / ".mid", by name
    std::vector<std::uintmax_t> fileSizes;     // with `files`: a folder read before is reused if unchanged
    std::vector<std::string> problems;         // files whose length could not be read
    RecordingNotes notes;                      // name, note, marks (recording.json; P21, P22)
    std::string notesProblem;                  // why recording.json could not be read
    std::int64_t notesStamp = 0;               // its size and time: unchanged notes are not read again (Y10)
};

// Start time from a folder name: "take-YYYYMMDD-HHMMSS" or (auto part)
// "YYYY-MM-DD HH-MM-SS", either with an optional "-N" suffix (1-9 digits).
// Seconds of the local clock time as if it were UTC: for the clock label.
bool recordingTime(const std::string& folderName, bool autoPart, std::int64_t& seconds);

// A local clock time (as recordingTime gives it) on one continuous scale, so
// pauses across a daylight-saving change are right (crosscheck #12, T7).
// Uses the computer's time zone; unchanged if it cannot be converted.
std::int64_t continuousTime(std::int64_t clockSeconds);

// The recordings in a takes folder (and its "auto" folder), oldest first.
// Folders whose name has no time, or that hold no .wav or .mid file, are left
// out. A folder in `previous` whose files have the same names and sizes is
// taken from there instead of reading its files again (T4).
std::vector<RecordingInfo> listRecordings(const std::filesystem::path& base,
                                          const std::vector<RecordingInfo>& previous = {});

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

// Recordings one after another in time order, in one stream (they must be
// sorted by start; auto parts and takes never overlap, P30):
// `pixelsPerSecond` for the recordings and for gaps up to `maxRealGap`
// seconds; a longer gap becomes a marker of `gapWidth` pixels labelled with
// its real length. Blocks are at least `minWidth` wide; a block never starts
// before the end of the one before it.
// `minWidths` (optional, one per recording): a wider minimum for that block
// (its label with a name, P21).
TimelineLayout layoutTimeline(const std::vector<RecordingInfo>& recordings, double pixelsPerSecond,
                              double maxRealGap, double gapWidth, double minWidth,
                              const std::vector<double>* minWidths = nullptr);

// The stream cut into lines of `lineWidth` pixels, as text is wrapped (P30):
// a block or gap that does not fit in the rest of the line goes to the next
// one; the short pause before it is left out at a line's start. A block wider
// than a line starts where it is (if at least `minPiece` is left there) and
// goes on at the start of the next line(s), in pieces; its last piece is at
// least `minPiece` wide (the last cut moves back) and ends exactly at the
// block's end (to = 1). Gaps are never cut, a line is at least as wide as the
// widest gap, and a gap goes to the next line with the block after it.
struct TimelinePiece {
    bool gap = false;       // a gap marker (layout.gaps) or a block (layout.blocks)
    std::size_t item = 0;   // its index there
    int line = 0;
    double x = 0.0;         // from the line's start
    double width = 0.0;
    double from = 0.0;      // the share of the block in this piece: from .. to (0 .. 1)
    double to = 1.0;
};
struct WrappedTimeline {
    std::vector<TimelinePiece> pieces;  // in drawing order; a block's pieces one after another
    int lines = 0;
};
WrappedTimeline wrapTimeline(const TimelineLayout& layout, double lineWidth, double minPiece);

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
