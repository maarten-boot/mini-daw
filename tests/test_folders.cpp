// Choosing a folder (P19, SPEC-UI-14) and checking an instrument folder (SPEC-VST-01a).

#include "minidaw/folder_browse.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/plugin_scan.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace minidaw;
using minidaw::test::TempDir;
namespace fs = std::filesystem;

TEST_CASE("T-UI-21: folder text cleaned, folders listed, where browsing starts", "[folders][T-UI-21]")
{
    // Spaces and one pair of quotes around a pasted path are removed.
    CHECK(cleanPathText("  /a/b  ") == "/a/b");
    CHECK(cleanPathText("\"C:\\Program Files\\VST3\"") == "C:\\Program Files\\VST3");
    CHECK(cleanPathText(" \" /x \" ") == "/x");
    CHECK(cleanPathText("\"") == "\"");
    CHECK(cleanPathText("a \"b\"") == "a \"b\"");
    CHECK(cleanPathText("   ").empty());

    TempDir dir;
    fs::create_directories(dir / "beta");
    fs::create_directories(dir / "Alpha");
    fs::create_directories(dir / "gamma" / "inner");
    fs::create_directories(dir / ".hidden");
    std::ofstream(dir / "file.txt") << "x";
    std::ofstream(dir / "zeta.vst3") << "a file, not a folder";

    // Folders only, by name with case ignored; hidden ones only on request.
    const auto list = listSubfolders(dir.path(), false);
    CHECK(list.error.empty());
    REQUIRE(list.folders.size() == 3);
    CHECK(list.folders[0].filename() == "Alpha");
    CHECK(list.folders[1].filename() == "beta");
    CHECK(list.folders[2].filename() == "gamma");
    CHECK(listSubfolders(dir.path(), true).folders.size() == 4);
    CHECK(listSubfolders(dir / "gamma" / "inner", false).folders.empty());
    // A folder that cannot be read: an error, no folders.
    const auto missing = listSubfolders(dir / "nope", false);
    CHECK(missing.folders.empty());
    CHECK(!missing.error.empty());

    // Start in the folder typed, else the nearest existing one above it, else the fallback.
    const fs::path fallback = dir / "beta";
    CHECK(browseStart(pathToUtf8(dir / "gamma"), fallback) == (dir / "gamma").lexically_normal());
    CHECK(browseStart(pathToUtf8(dir / "gamma" / "not" / "there"), fallback) == (dir / "gamma").lexically_normal());
    CHECK(browseStart("\"" + pathToUtf8(dir / "gamma") + "\"", fallback) == (dir / "gamma").lexically_normal());
    CHECK(browseStart(pathToUtf8(dir / "file.txt"), fallback) == dir.path().lexically_normal());
    CHECK(browseStart("", fallback) == fallback);

    CHECK(!filesystemRoots().empty());
}

TEST_CASE("T-VST-32: an instrument folder is checked before it is added", "[folders][vst][T-VST-32]")
{
    TempDir dir;
    fs::create_directories(dir / "empty");
    fs::create_directories(dir / "plugins" / "Synth.vst3" / "Contents");
    fs::create_directories(dir / "standard");
    std::ofstream(dir / "file.txt") << "x";
    const std::vector<fs::path> standard = {dir / "standard"};
    std::string note;

    // Good: no error; a remark when there is no .vst3 in it (it can be added anyway).
    CHECK(checkInstrumentFolder(dir / "plugins", {}, standard, note).empty());
    CHECK(note.empty());
    CHECK(checkInstrumentFolder(dir / "empty", {}, standard, note).empty());
    CHECK(!note.empty());

    // Refused: empty text, missing, a file, a standard folder, listed already (also spelled otherwise), 8 added.
    CHECK(!checkInstrumentFolder(fs::path(), {}, standard, note).empty());
    CHECK(!checkInstrumentFolder(dir / "missing", {}, standard, note).empty());
    CHECK(!checkInstrumentFolder(dir / "file.txt", {}, standard, note).empty());
    CHECK(!checkInstrumentFolder(dir / "standard", {}, standard, note).empty());
    const std::vector<std::string> added = {pathToUtf8(dir / "plugins")};
    CHECK(!checkInstrumentFolder(dir / "plugins", added, standard, note).empty());
    CHECK(!checkInstrumentFolder(dir / "plugins" / ".." / "plugins", added, standard, note).empty());
    const std::vector<std::string> eight(8, pathToUtf8(dir / "empty"));
    CHECK(!checkInstrumentFolder(dir / "plugins", eight, standard, note).empty());
}
