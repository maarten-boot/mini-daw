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
// comes from the recording, the MIDI track is muted instead. Recording names
// and notes become the clip's name and comment, marks made while recording
// become Markers. MIDI messages other than notes, MIDI outputs and recordings
// that are not on a track are left out (listed in the result).

#include "minidaw/project.hpp"

#include <filesystem>
#include <string>
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
    std::vector<std::string> leftOut;  // one line per thing not exported, for the user
};

// Writes `outFile` (via "<outFile>.tmp" and a rename; nothing is left behind on
// failure). `sampleRate`: the rate of the takes' timeline positions (the stream's
// rate; 0 = the project's). `title`: the project's name. Nothing in the
// project is changed.
DawExportResult exportDawProject(const ProjectData& project, unsigned int sampleRate, const std::string& title,
                                 const std::string& appVersion, const std::filesystem::path& outFile);

// Text made safe for XML 1.0: bytes that are not UTF-8 become U+FFFD, control
// characters other than tab, line feed and carriage return are left out.
std::string xmlSafeText(const std::string& text);

// A number as XML text: the shortest form that reads back as the same double.
std::string xmlNumber(double value);

} // namespace minidaw
