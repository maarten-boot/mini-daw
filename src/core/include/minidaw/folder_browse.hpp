#pragma once

// Choosing a folder (P19, SPEC-UI-14): the parts of the folder browser that do
// not need the GUI, so they can be tested.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

// Text typed or pasted as a path: spaces around it and one pair of quotes
// around it removed (Windows Explorer "Copy as path" adds quotes), and a
// separator at the end (not that of a root such as "/" or "C:\\").
std::string cleanPathText(std::string_view text);

struct FolderListing {
    std::vector<std::filesystem::path> folders;  // full paths, by name (case ignored)
    std::string error;                           // the folder could not be read
};

// The folders inside `folder` (not files). Names starting with '.' are left
// out unless `showHidden`. Entries that cannot be read are skipped.
FolderListing listSubfolders(const std::filesystem::path& folder, bool showHidden);

// Where a browser starts: the folder in `text` if it exists, else the nearest
// existing folder above it, else `fallback`.
std::filesystem::path browseStart(std::string_view text, const std::filesystem::path& fallback);

// The tops of the file system: "/" on Linux; the drives on Windows.
std::vector<std::filesystem::path> filesystemRoots();

// The user's home folder (empty if not known).
std::filesystem::path homeFolder();

} // namespace minidaw
