#pragma once

// A recording's name and notes (P21, SPEC-PLAY-08) and its marks made while
// recording (P22, SPEC-REC-09), kept in `recording.json` in its folder (a take
// folder or an auto record part). The folder's own name keeps its date and
// time, so the timeline places it the same as before.
//
//   {"format": "mini-daw recording", "version": 1, "name": "Chorus",
//    "note": "…", "marks": [{"seconds": 83.2, "text": "good part"}]}
//
// Deleting a recording (P21) moves its folder into "deleted" in the takes
// folder ("deleted/<take folder>" or "deleted/auto/<part>"), where the
// timeline does not look; it can be moved back by hand.

#include <filesystem>
#include <string>
#include <vector>

namespace minidaw {

struct RecordingMark {
    double seconds = 0.0;  // from the recording's start
    std::string text;
};

struct RecordingNotes {
    std::string name;   // empty: none (the timeline shows the time only)
    std::string note;   // any text, several lines
    std::vector<RecordingMark> marks;
    bool empty() const { return name.empty() && note.empty() && marks.empty(); }
};

inline constexpr const char* kRecordingNotesFile = "recording.json";

// A missing file gives empty notes (true). A damaged one: false with a reason,
// `notes` empty (the recording is still shown).
bool loadRecordingNotes(const std::filesystem::path& folder, RecordingNotes& notes, std::string& error);
// Writes the file (via a temporary file and a rename); empty notes remove it.
// The folder must be there (a recording moved away is not made again, Y9).
// `sync`: force it to the disk (off for marks made while recording, Y8).
bool saveRecordingNotes(const std::filesystem::path& folder, const RecordingNotes& notes, std::string& error,
                        bool sync = true);

// Adds a mark (P22, SPEC-REC-09) to the recording in `folder` (made at Record or
// at a part's start; a MIDI-only take has no file in it yet), keeping its name,
// note and the other marks; marks stay sorted by time. A damaged file is not
// overwritten (false with the reason). Not forced to the disk (Y8).
bool addRecordingMark(const std::filesystem::path& folder, const RecordingMark& mark, std::string& error);

// A folder path without a separator at its end ("D:\Takes\" → "D:\Takes"; a
// root such as "/" or "C:\" stays), normalised, so two spellings compare equal (Y1).
std::filesystem::path withoutTrailingSeparator(const std::filesystem::path& folder);

// Moves `folder` (a recording in `takesFolder`, or in its "auto" folder) to
// takesFolder/deleted/[auto/]<name>, with "-2", "-3" … when that exists.
// `movedTo` is where it went.
bool moveRecordingToDeleted(const std::filesystem::path& takesFolder, const std::filesystem::path& folder,
                            std::filesystem::path& movedTo, std::string& error);

// (Whether a track's take lies in a recording: isInsideFolder in project.hpp.)

} // namespace minidaw
