// Presets (PL2, SPEC-VST-07a): .vstpreset format, standard folders, listing.

#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& p, const std::string& text = "x")
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

} // namespace

TEST_CASE("T-VST-34: .vstpreset files are written and read back; damaged ones are refused", "[presets][T-VST-34]")
{
    VstPresetData d;
    d.classId = "0123456789ABCDEF0123456789ABCDEF";
    d.component = std::string("comp\0state", 10);
    d.controller = "controller state";
    const std::string bytes = encodeVstPreset(d);
    CHECK(bytes.substr(0, 4) == "VST3");
    CHECK(bytes.substr(8, 32) == d.classId);

    std::string err;
    auto back = decodeVstPreset(bytes, err);
    REQUIRE(back);
    CHECK(back->classId == d.classId);
    CHECK(back->component == d.component);
    CHECK(back->controller == d.controller);

    SECTION("without a controller state")
    {
        d.controller.clear();
        auto b = decodeVstPreset(encodeVstPreset(d), err);
        REQUIRE(b);
        CHECK(b->controller.empty());
    }
    SECTION("not a preset, cut off, chunk outside the file, no Comp chunk")
    {
        CHECK_FALSE(decodeVstPreset("hello", err));
        CHECK_FALSE(decodeVstPreset(bytes.substr(0, bytes.size() - 10), err));
        std::string bad = bytes;
        bad[bad.size() - 9] = '\x7f';  // the last entry's size: far beyond the file
        CHECK_FALSE(decodeVstPreset(bad, err));
        std::string noComp = bytes;
        noComp.replace(noComp.find("Comp", 48), 4, "Xomp");
        CHECK_FALSE(decodeVstPreset(noComp, err));
        CHECK(err.find("no instrument state") != std::string::npos);
    }
}

TEST_CASE("T-VST-35: preset folders of a plugin and the files in them", "[presets][T-VST-35]")
{
    test::TempDir tmp;
    const fs::path bundle = tmp.path() / "Synth.vst3";
    const fs::path module = bundle / "Contents" / "x86_64-linux" / "Synth.so";
    touch(module);
    const PresetFolders f = presetFolders("Ven:dor", "My/Synth", module);
    REQUIRE_FALSE(f.all.empty());
    CHECK(f.all.front() == f.user);
    // Names made safe for a folder; the bundle's factory presets folder last.
    CHECK(pathToUtf8(f.user).find("Ven_dor") != std::string::npos);
    CHECK(pathToUtf8(f.user).find("My_Synth") != std::string::npos);
    CHECK(f.all.back() == bundle / "Contents" / "Resources" / "Presets");
    CHECK(userPresetPath(f, "Warm: pad").filename() == "Warm_ pad.vstpreset");

    // Listing: only .vstpreset, subfolders as groups, sorted, the user's marked.
    PresetFolders mine;
    mine.user = tmp.path() / "user";
    mine.all = std::vector<fs::path>{mine.user, tmp.path() / "factory", tmp.path() / "missing"};
    touch(mine.user / "b.vstpreset");
    touch(mine.user / "A.vstpreset");
    touch(mine.user / "notes.txt");
    touch(tmp.path() / "factory" / "Bass" / "deep.VSTPRESET");
    touch(tmp.path() / "factory" / "café.vstpreset");
    bool truncated = true;
    const auto list = listPresetFiles(mine, 100, truncated);
    CHECK_FALSE(truncated);
    REQUIRE(list.size() == 4);
    CHECK(list[0].name == "A");
    CHECK(list[0].user);
    CHECK(list[1].name == "b");
    CHECK(list[2].name == "café");
    CHECK(list[2].group.empty());
    CHECK_FALSE(list[2].user);
    CHECK(list[3].name == "deep");
    CHECK(list[3].group == "Bass");

    const auto two = listPresetFiles(mine, 2, truncated);
    CHECK(two.size() == 2);
    CHECK(truncated);
}

TEST_CASE("T-VST-34: preset files are replaced safely", "[presets][T-VST-34]")
{
    test::TempDir tmp;
    const fs::path p = tmp.path() / "new folder" / "ünï.vstpreset";
    std::string err;
    REQUIRE(writeFileReplacing(p, "first", err));
    REQUIRE(writeFileReplacing(p, "second", err));
    std::string back;
    REQUIRE(readWholeFile(p, back, err));
    CHECK(back == "second");
    CHECK_FALSE(fs::exists(p.string() + ".tmp"));
    CHECK_FALSE(readWholeFile(tmp.path() / "none.vstpreset", back, err));
}
