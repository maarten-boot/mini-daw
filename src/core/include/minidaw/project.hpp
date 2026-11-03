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
// `folder/takes/imported/<its take folder name>/<file>` and points the track
// at the copy. An existing file there is reused only when its content is the
// same; otherwise the copy gets a free name (`<file>-2`, `-3`, ...), so an
// earlier copy is never overwritten (crosscheck #7, N3). A track whose take
// cannot be copied keeps its old path.
struct ImportResult {
    std::vector<std::string> copied;   // "source -> copy"
    std::vector<std::string> missing;  // the take file is gone: nothing to copy
    std::vector<std::string> failed;   // the copy failed (disk full, no permission)
};
ImportResult importTakes(const std::filesystem::path& folder, std::vector<Track>& tracks);

// The two files have the same size and content.
bool sameFileContent(const std::filesystem::path& a, const std::filesystem::path& b);

// `file` lies below `folder` (not the folder itself).
bool isInsideFolder(const std::filesystem::path& file, const std::filesystem::path& folder);

// Folders under `base` that hold a project (one level deep), by name.
std::vector<std::filesystem::path> listProjects(const std::filesystem::path& base);

} // namespace minidaw
