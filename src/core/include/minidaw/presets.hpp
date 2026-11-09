#pragma once

// Presets for VST3 instruments (PL2, SPEC-VST-07): .vstpreset files and the
// standard folders they live in. The plugin's own program list is read by
// Vst3Instrument (instrument.hpp).
//
// The .vstpreset format (VST 3 SDK, vstpresetfile.cpp), little-endian:
//   header  "VST3", int32 version (1), 32 ASCII characters class ID,
//           int64 offset of the chunk list
//   chunks  the data, one after the other
//   list    "List", int32 count, per entry: 4-character ID, int64 offset, int64 size
// Chunks used: "Comp" (the processor state, required), "Cont" (the controller
// state), "Info" (XML meta information, kept when read, not written).

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

struct VstPresetData {
    std::string classId;     // the 32 characters as stored in the file
    std::string component;   // "Comp"
    std::string controller;  // "Cont" (may be empty)
};

std::string encodeVstPreset(const VstPresetData& data);
// nullopt with a reason when the bytes are not a usable .vstpreset.
std::optional<VstPresetData> decodeVstPreset(std::string_view bytes, std::string& error);

// The standard preset folders of one plugin (VST 3 "Preset Locations"), each
// <root>/<vendor>/<plugin name>, the names made safe for file names:
// Windows: Documents\VST3 Presets (the user's, where Save preset writes),
// %APPDATA%\VST3 Presets, %PROGRAMDATA%\VST3 Presets; Linux: ~/.vst3/presets,
// /usr/share/vst3/presets, /usr/local/share/vst3/presets. Also the plugin's
// own bundle: <module>.vst3/Contents/Resources/Presets (factory presets).
struct PresetFolders {
    std::filesystem::path user;               // where new presets are saved
    std::vector<std::filesystem::path> all;   // user first; may not exist
};
PresetFolders presetFolders(const std::string& vendor, const std::string& pluginName,
                            const std::filesystem::path& modulePath);

struct PresetFileEntry {
    std::string name;             // the file name without .vstpreset (UTF-8)
    std::string group;            // subfolder below its preset folder ("" at the top), '/' separated
    std::filesystem::path path;
    bool user = false;            // in the user's own folder
};

// Every .vstpreset in the folders (and their subfolders), sorted by folder
// (user first), group and name (case-insensitive). At most `limit` files
// (`moreFiles` if there were more); each folder is searched through at most
// `searchLimit` entries (`searchCut` if one was larger). Unreadable folders
// are skipped.
struct PresetListing {
    std::vector<PresetFileEntry> files;
    bool moreFiles = false;
    bool searchCut = false;
};
PresetListing listPresetFiles(const PresetFolders& folders, std::size_t limit, std::size_t searchLimit = 50000);

// A preset or folder name made safe for a file name on every system (crosscheck #16,
// X3 / X8): safeFileName, Windows device names (CON, NUL, COM1 …) get a "_" in front,
// at most 120 bytes (whole UTF-8 characters). "Preset" when nothing is left.
std::string safePresetName(std::string_view name);
// Where "Save preset" writes a preset called `name`: <user folder>/<safePresetName>.vstpreset.
std::filesystem::path userPresetPath(const PresetFolders& folders, const std::string& name);

// --- The menu's entries: programs first, then files (crosscheck #16, X2 / X11) ---
// Each entry has an identity ("p:<index>" for a program, "f:<path>" for a file)
// and a label ("name", or "group/name" for a file).
struct PresetChoice {
    std::string id;
    std::string label;
};
std::vector<PresetChoice> presetChoices(const std::vector<std::string>& programs,
                                        const std::vector<PresetFileEntry>& files);
// The current entry: by identity; else by label (the first with it); else, when
// nothing was chosen (id and label empty), the plugin's current program; else -1.
int findPresetChoice(const std::vector<PresetChoice>& choices, const std::string& id, const std::string& label,
                     int currentProgram);
// One step from `current` (-1: none) by +1 / -1, wrapping round; from none, +1 gives
// the first and -1 the last. -1 when there are no entries.
int stepPresetChoice(std::size_t count, int current, int step);

// --- Program-change parameter arithmetic (crosscheck #16, X5) ---------------
// How many steps the parameter has: its stepCount, or (a continuous one) the
// list's size - 1. Index <-> normalized value; how many entries are reachable.
int programSteps(int stepCount, std::size_t listSize);
double programNormalized(int index, int steps);
int programIndex(double normalized, int steps);
std::size_t reachablePrograms(int steps, std::size_t listSize);

// True when `text` is 32 hexadecimal characters (a class ID in a .vstpreset).
bool isClassIdText(std::string_view text);

// Whole files, UTF-8 safe paths. writeFileReplacing writes a temporary file
// next to it and renames it over the old one (a crash leaves old or new).
bool readWholeFile(const std::filesystem::path& path, std::string& bytes, std::string& error);
bool writeFileReplacing(const std::filesystem::path& path, const std::string& bytes, std::string& error);

} // namespace minidaw
