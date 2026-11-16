#pragma once

// DAWproject export (P27, SPEC-PRJ-04): the open project as a `.dawproject`
// file, the open exchange format of Bitwig, PreSonus, Steinberg and others
// (https://github.com/bitwig/dawproject, MIT, format 1.0).
//
// The file is a ZIP with
//   project.xml    tracks, mixer, arrangement (valid against Project.xsd)
//   metadata.xml   title and the project note (MetaData.xsd)
//   audio/…        the WAV takes, stored as they are
//   plugins/…      each VST3 instrument's state as a .vstpreset (as the
//                  format's reference implementation says for VST3)
//
// mini-daw has no tempo: all times are in seconds (the Transport gets a nominal
// 120 bpm, 4/4). Per track: a Track with a Channel (volume as a linear gain,
// mute, solo, pan centre) going to a master channel; its take as one clip (a
// WAV as Audio, a MIDI file as Notes); a VST3 instrument as Vst3Plugin. A
// linked "… sound" track becomes an ordinary audio track, muted when the
// instrument plays the take (so the sound is not doubled); when the sound
// comes from the recording, the MIDI track is muted instead; the MIDI track's
// mute also mutes it and its solo solos it (as mini-daw plays them). Its
// channel count is the WAV's. Recording names
// and notes become the clip's name and comment, marks made while recording
// become Markers (counted from the recording's earliest take). Auto record
// parts no track plays go on one muted "Auto record" track. MIDI messages
// other than notes and MIDI outputs are left out (listed in the result); so
// are Record takes no track plays (the caller lists those).

#include "minidaw/project.hpp"
#include "minidaw/timeline.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

struct DawExportResult {
    bool ok = false;
    std::string error;              // why nothing was written
    int tracks = 0;                 // without the master track
    int audioClips = 0;
    int noteClips = 0;
    int notes = 0;
    int markers = 0;
    int instruments = 0;
    int autoRecordParts = 0;        // on the "Auto record" track
    std::vector<std::string> leftOut;  // one line per thing not exported, for the user
};

// Writes `outFile` (via "<outFile>.tmp" and a rename; nothing is left behind on
// failure). `sampleRate`: the rate of the takes' timeline positions (the stream's
// rate; 0 = the project's). `title`: the project's name. `autoParts`: auto
// record parts that no track plays (from the Timeline), put on one muted
// "Auto record" track, a lane per file, each clip at 0 s. Nothing in the
// project is changed.
DawExportResult exportDawProject(const ProjectData& project, unsigned int sampleRate, const std::string& title,
                                 const std::string& appVersion, const std::filesystem::path& outFile,
                                 const std::vector<RecordingInfo>& autoParts = {});

// Text made safe for XML 1.0: bytes that are not UTF-8 become U+FFFD, control
// characters other than tab, line feed and carriage return are left out.
std::string xmlSafeText(const std::string& text);

// A number as XML text: the shortest form that reads back as the same double.
std::string xmlNumber(double value);

// A VST3 class ID (32 hex digits, as the SDK's UID::toString writes it: the
// UUID's fields in order on every platform) in the form the format asks for:
// "the canonical textual representation of the UUID (8-4-4-4-12 with no
// braces)", upper case. Anything else is returned as it is.
std::string vst3DeviceId(const std::string& classId);

// The export window's name field (crosscheck #22 CC6): spaces around it and a
// ".dawproject" typed at the end (any case) removed; "" when nothing is left.
std::string dawExportName(std::string_view typed);
// The file name for it: made safe on every system (safePresetName, so also
// Windows device names such as CON) plus ".dawproject"; "" for an empty name.
std::string dawExportFileName(std::string_view typed);

// --- Import (P28, SPEC-PRJ-05) ------------------------------------------------
//
// A new mini-daw project from a .dawproject file, as far as mini-daw can hold
// it. Times in beats are converted with the file's tempo (Transport); every
// imported file goes into one recording folder `takes/<takeFolderName>`.
//   - Tracks: in file order; group tracks give their tracks (the group's own
//     mix is left out); the master and effect return tracks are left out. A
//     track's notes become a MIDI track, its audio an audio track (both: two
//     tracks). A nested lane of a track becomes a track of its own
//     ("<track> <lane>"), so lanes on top of each other stay apart.
//   - Mixer: volume (linear or dB, kept within -60 .. +6 dB), mute, solo.
//   - Notes: all note clips of a track in one MIDI take (playStart / playStop,
//     loops, the clip's length applied; velocities 0..1 to 1..127).
//   - Audio: all audio clips of a track rendered into one WAV at
//     `sampleRate` (from the first clip's start; overlapping clips added),
//     with playStart / playStop, loops and fades; another sample rate
//     converted (SincKernel); clips in Warps only when they play at their
//     own speed (else left out: mini-daw cannot stretch). WAV files only
//     (16/24/32-bit, float), inside the file or external.
//   - A VST3 instrument on a notes track: its class ID and, from its
//     .vstpreset, its state. Other plugins and devices are left out.
//   - Markers become marks of the recording; the title its name; the
//     metadata comment the project note.
// Left out, with a line each in `leftOut`: automation, sends, pan, effects,
// CLAP / VST2 / AU plugins, the clip launcher (scenes), time-stretched clips,
// other audio formats, tempo changes, disabled clips. A damaged or unknown
// file, or a newer major version, gives `error` and creates nothing; a
// failure while writing removes what was written.
struct DawImportResult {
    bool ok = false;
    std::string error;
    std::string application;        // what made the file ("Bitwig Studio 5.2")
    std::string title;              // the metadata title, else the file's name
    int tracks = 0;
    int audioClips = 0;
    int noteClips = 0;
    int notes = 0;
    int instruments = 0;
    int markers = 0;
    std::vector<std::string> leftOut;
};

DawImportResult importDawProject(const std::filesystem::path& file, const std::filesystem::path& projectFolder,
                                 unsigned int sampleRate, const std::string& takeFolderName);

// A VST3 deviceID ("01234567-89AB-CDEF-0123-456789ABCDEF", with or without
// braces or dashes, any case) as mini-daw's class ID (32 hex digits, upper
// case); "" when it is not one.
std::string vst3ClassFromDeviceId(const std::string& deviceId);

} // namespace minidaw
