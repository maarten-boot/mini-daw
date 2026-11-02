#pragma once

// Projects (SPEC-PRJ-01, 02).
//
// A project is a folder with `project.json` and the take files under
// `takes/`. The JSON file holds the track list (names, inputs, MIDI ports,
// arm/monitor, volume/mute/solo, and each track's take with its position on
// the timeline). Take paths inside the project folder are stored relative to
// it, with '/' separators, in UTF-8, so the folder can be moved or copied as
// a whole. Takes that live outside the project are copied into
// `takes/imported/` (importTakes), so a project never depends on other folders.
//
// Saving writes `project.json.tmp`, forces it to disk and renames it over the
// old file, so a crash leaves either the old or the new file, never half of one.

#include "minidaw/track.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace minidaw {

inline constexpr const char* kProjectFileName = "project.json";
inline constexpr int kProjectFormatVersion = 1;

struct ProjectData {
    std::vector<Track> tracks;
    unsigned int sampleRate = 0;   // stream rate when it was saved (informative)
};

// Serialise / parse. `folder` is the project folder: take paths inside it
// become relative. Parsing never throws: errors go to `error` (false), and
// unknown or bad fields to `warnings` (the rest is still loaded).
std::string projectToJson(const ProjectData& data, const std::filesystem::path& folder);
bool projectFromJson(const std::string& text, const std::filesystem::path& folder, ProjectData& data,
                     std::vector<std::string>& warnings, std::string& error);

// The folder holds a project file.
bool isProjectFolder(const std::filesystem::path& folder);

// Creates the folder (and parents). Refuses a folder that already holds a
// project, or a file in its place.
bool createProjectFolder(const std::filesystem::path& folder, std::string& error);

bool saveProject(const std::filesystem::path& folder, const ProjectData& data, std::string& error);
bool loadProject(const std::filesystem::path& folder, ProjectData& data, std::vector<std::string>& warnings,
                 std::string& error);

// Copies every take that is not inside `folder` to
// `folder/takes/imported/<its take folder name>/<file>` (an existing file
// there with the same size is reused) and points the track at the copy.
// Returns one message per copied file in `copied`; errors (missing source,
// disk full) are returned, and that track keeps its old path.
std::vector<std::string> importTakes(const std::filesystem::path& folder, std::vector<Track>& tracks,
                                     std::vector<std::string>& copied);

// The take is inside the project folder.
bool isInsideFolder(const std::filesystem::path& file, const std::filesystem::path& folder);

// Folders under `base` that hold a project (one level deep), by name.
std::vector<std::filesystem::path> listProjects(const std::filesystem::path& base);

} // namespace minidaw
