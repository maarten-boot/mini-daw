// Choosing a folder (P19, SPEC-UI-14) and checking an instrument folder (SPEC-VST-01a).

#include "minidaw/folder_browse.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/plugin_scan.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <fstream>

#ifndef _WIN32
#include <unistd.h>
#endif

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

TEST_CASE("T-UI-21b: odd folder names and paths (crosscheck #14)", "[folders][T-UI-21]")
{
    TempDir dir;
    // A separator at the end is dropped, not that of a root (V6).
    CHECK(cleanPathText("/a/b/") == "/a/b");
    CHECK(cleanPathText("/a/b//") == "/a/b");
    CHECK(cleanPathText("/") == "/");
#ifdef _WIN32
    CHECK(cleanPathText("C:\\") == "C:\\");
    CHECK(cleanPathText("C:\\x\\") == "C:\\x");
    CHECK(cleanPathText("\\\\srv\\share\\") == "\\\\srv\\share");
#endif
    fs::create_directories(dir / "gamma");
    CHECK(browseStart(pathToUtf8(dir / "gamma") + "/", dir.path()) == (dir / "gamma").lexically_normal());

    // Non-ASCII names (UTF-8 in the GUI, wide on Windows).
    fs::create_directories(dir.path() / pathFromUtf8("Gitaar \xc3\xa9"));
    fs::create_directories(dir.path() / pathFromUtf8("\xe9\x9f\xb3\xe6\xa5\xbd"));  // CJK
    auto list = listSubfolders(dir.path(), false);
    CHECK(list.error.empty());
    CHECK(list.folders.size() == 3);
    CHECK(browseStart(pathToUtf8(dir.path() / pathFromUtf8("Gitaar \xc3\xa9")), dir.path()) ==
          (dir.path() / pathFromUtf8("Gitaar \xc3\xa9")).lexically_normal());

#ifndef _WIN32
    // V1: a name that is not UTF-8 (Latin-1 "caf\xe9") is listed, not fatal.
    fs::create_directories(dir.path() / fs::path(std::string("caf\xe9")));
    list = listSubfolders(dir.path(), false);
    CHECK(list.error.empty());
    CHECK(list.folders.size() == 4);
    // Links: one to a folder is listed, a dangling one is skipped.
    fs::create_directory_symlink(dir / "gamma", dir / "link-to-gamma");
    fs::create_directory_symlink(dir / "gone", dir / "dangling");
    list = listSubfolders(dir.path(), false);
    CHECK(list.folders.size() == 5);
    // A folder that cannot be read: an error, no crash.
    fs::create_directories(dir / "closed" / "inner");
    fs::permissions(dir / "closed", fs::perms::none);
    const auto closed = listSubfolders(dir / "closed", false);
    fs::permissions(dir / "closed", fs::perms::owner_all);
    if (::geteuid() != 0) {
        CHECK(closed.folders.empty());
        CHECK(!closed.error.empty());
    }
#endif
}

TEST_CASE("T-VST-32b: adding instrument folders (crosscheck #14)", "[folders][vst][T-VST-32]")
{
    TempDir dir;
    fs::create_directories(dir / "std" / "VST3");
    fs::create_directories(dir / "mine" / "deep" / "Synth.vst3");
    for (int i = 0; i < 50; ++i) fs::create_directories(dir / "big" / ("f" + std::to_string(i)));
    const std::vector<fs::path> standard = {dir / "std" / "VST3"};
    std::vector<std::string> added;
    std::string note;

    // Cleaned text is added; the same folder again is refused (V10).
    CHECK(addInstrumentFolder(added, "  \"" + pathToUtf8(dir / "mine") + "/\" ", standard, note).empty());
    REQUIRE(added.size() == 1);
    CHECK(added[0] == pathToUtf8(dir / "mine"));
    CHECK(note.empty());
    CHECK(!addInstrumentFolder(added, pathToUtf8(dir / "mine"), standard, note).empty());
    CHECK(added.size() == 1);

    // A folder holding a standard folder: added with a remark; inside one: added (V3).
    CHECK(addInstrumentFolder(added, pathToUtf8(dir / "std"), standard, note).empty());
    CHECK(note.find("standard folder") != std::string::npos);
    fs::create_directories(dir / "std" / "VST3" / "Vendor");
    CHECK(addInstrumentFolder(added, pathToUtf8(dir / "std" / "VST3" / "Vendor"), standard, note).empty());

    // A large folder is not searched to the end (V3).
    CHECK(checkInstrumentFolder(dir / "big", {}, standard, note, 10).empty());
    CHECK(note.find("not searched to the end") != std::string::npos);
    bool complete = false;
    CHECK(hasVst3Module(dir / "mine", 100, complete));
    CHECK(complete);

    // The top of a file system is refused.
    CHECK(!checkInstrumentFolder(filesystemRoots().front(), {}, standard, note).empty());
#ifdef _WIN32
    // Other case, same folder (Windows).
    std::string upper = pathToUtf8(dir / "mine");
    for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(!checkInstrumentFolder(pathFromUtf8(upper), added, standard, note).empty());
#endif
}
