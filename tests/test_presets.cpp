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
        bad[bad.size() - 1] = '\x7f';  // the last entry's size (its last byte): far beyond the file (X12)
        CHECK_FALSE(decodeVstPreset(bad, err));
        CHECK(err.find("outside the file") != std::string::npos);
        std::string farOffset = bytes;
        farOffset[farOffset.size() - 9] = '\x7f';  // the last entry's offset
        CHECK_FALSE(decodeVstPreset(farOffset, err));
        std::string badId = bytes;
        badId[8] = 'x';  // not hexadecimal (X7)
        CHECK_FALSE(decodeVstPreset(badId, err));
        CHECK(err.find("class ID") != std::string::npos);
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
    CHECK(userPresetPath(f, "CON").filename() == "_CON.vstpreset");  // a Windows device name (X8)

    // Listing: only .vstpreset, subfolders as groups, sorted, the user's marked.
    PresetFolders mine;
    mine.user = tmp.path() / "user";
    mine.all = std::vector<fs::path>{mine.user, tmp.path() / "factory", tmp.path() / "missing"};
    touch(mine.user / "b.vstpreset");
    touch(mine.user / "A.vstpreset");
    touch(mine.user / "notes.txt");
    touch(tmp.path() / "factory" / "Bass" / "deep.VSTPRESET");
    touch(tmp.path() / "factory" / pathFromUtf8("café.vstpreset"));  // UTF-8 as mini-daw makes names (X0)
    const PresetListing listing = listPresetFiles(mine, 100);
    const auto& list = listing.files;
    CHECK_FALSE(listing.moreFiles);
    CHECK_FALSE(listing.searchCut);
    REQUIRE(list.size() == 4);
    CHECK(list[0].name == "A");
    CHECK(list[0].user);
    CHECK(list[1].name == "b");
    CHECK(list[2].name == "café");
    CHECK(list[2].group.empty());
    CHECK_FALSE(list[2].user);
    CHECK(list[3].name == "deep");
    CHECK(list[3].group == "Bass");

    const PresetListing two = listPresetFiles(mine, 2);
    CHECK(two.files.size() == 2);
    CHECK(two.moreFiles);
    CHECK_FALSE(two.searchCut);
    const PresetListing cut = listPresetFiles(mine, 100, 2);  // at most 2 entries searched per folder (X9)
    CHECK(cut.searchCut);
    CHECK_FALSE(cut.moreFiles);
}

TEST_CASE("T-VST-34: preset files are replaced safely", "[presets][T-VST-34]")
{
    test::TempDir tmp;
    const fs::path p = tmp.path() / "new folder" / pathFromUtf8("ünï.vstpreset");
    std::string err;
    REQUIRE(writeFileReplacing(p, "first", err));
    REQUIRE(writeFileReplacing(p, "second", err));
    std::string back;
    REQUIRE(readWholeFile(p, back, err));
    CHECK(back == "second");
    CHECK_FALSE(fs::exists(p.string() + ".tmp"));
    CHECK_FALSE(readWholeFile(tmp.path() / "none.vstpreset", back, err));
}

TEST_CASE("T-VST-35: preset names safe for every system", "[presets][T-VST-35]")
{
    CHECK(safePresetName("Warm: pad") == "Warm_ pad");
    CHECK(safePresetName("con") == "_con");
    CHECK(safePresetName("NUL.txt") == "_NUL.txt");
    CHECK(safePresetName("COM1") == "_COM1");
    CHECK(safePresetName("COM10") == "COM10");
    CHECK(safePresetName("Console") == "Console");
    CHECK(safePresetName("...") == "Preset");
    CHECK(safePresetName("track") == "track");
    const std::string longName(200, 'a');
    CHECK(safePresetName(longName).size() == 120);
    std::string accents;
    for (int i = 0; i < 100; ++i) accents += "\xc3\xa9";  // 200 bytes of "é"
    const std::string cutAccents = safePresetName(accents);
    CHECK(cutAccents.size() == 120);  // whole characters only
    CHECK((static_cast<unsigned char>(cutAccents.back()) & 0xC0) == 0x80);
}

TEST_CASE("T-VST-36: the menu's entries, the current one and stepping (programs, then files)", "[presets][T-VST-36]")
{
    std::vector<PresetFileEntry> files(3);
    files[0].name = "Init";
    files[0].path = "/u/Init.vstpreset";
    files[1].name = "Init";
    files[1].path = "/shared/Init.vstpreset";  // the same name in another folder (X2)
    files[2].name = "Deep";
    files[2].group = "Bass";
    files[2].path = "/shared/Bass/Deep.vstpreset";
    const auto c = presetChoices({"Soft", "Init"}, files);
    REQUIRE(c.size() == 5);
    CHECK(c[0].id == "p:0");
    CHECK(c[1].label == "Init");
    CHECK(c[4].label == "Bass/Deep");

    // By identity, also when the label is there twice (or three times).
    CHECK(findPresetChoice(c, "f:/shared/Init.vstpreset", "Init", -1) == 3);
    CHECK(findPresetChoice(c, "", "Init", -1) == 1);       // label only: the first
    CHECK(findPresetChoice(c, "", "", 0) == 0);            // nothing chosen: the plugin's program (X11)
    CHECK(findPresetChoice(c, "", "Gone", 0) == -1);       // a name no longer there
    CHECK(findPresetChoice(c, "f:/gone", "", -1) == -1);

    // Stepping from the later "Init" reaches the entries after it.
    CHECK(stepPresetChoice(c.size(), 3, +1) == 4);
    CHECK(stepPresetChoice(c.size(), 4, +1) == 0);   // wraps round
    CHECK(stepPresetChoice(c.size(), 0, -1) == 4);
    CHECK(stepPresetChoice(c.size(), -1, +1) == 0);  // from none: first / last
    CHECK(stepPresetChoice(c.size(), -1, -1) == 4);
    CHECK(stepPresetChoice(0, -1, +1) == -1);
}

TEST_CASE("T-VST-33: program-change parameter arithmetic", "[presets][T-VST-33]")
{
    CHECK(programSteps(2, 3) == 2);
    CHECK(programSteps(0, 5) == 4);  // continuous parameter: as many steps as the list has (X5)
    CHECK(programSteps(0, 1) == 0);
    CHECK(programNormalized(1, 2) == 0.5);
    CHECK(programNormalized(5, 2) == 1.0);
    CHECK(programNormalized(1, 0) == 0.0);
    CHECK(programIndex(0.5, 2) == 1);
    CHECK(programIndex(0.74, 4) == 3);
    CHECK(programIndex(2.0, 4) == 4);
    CHECK(reachablePrograms(2, 128) == 3);  // a list longer than the parameter reaches
    CHECK(reachablePrograms(4, 3) == 3);
    CHECK(isClassIdText("0123456789abcdefABCDEF0123456789"));
    CHECK_FALSE(isClassIdText("0123456789abcdefABCDEF012345678"));
    CHECK_FALSE(isClassIdText("0123456789abcdefABCDEF012345678g"));
}

// ---------------------------------------------------------------------------
// Compatibility with other hosts: the SDK's own PresetFile writes and reads.
#include "pluginterfaces/base/funknown.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"

TEST_CASE("T-VST-34: .vstpreset files from the SDK are read, ours are read by the SDK", "[presets][T-VST-34]")
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    const FUID uid(0x6D696E69, 0x64617774, 0x53796E50, 0x726F6331);
    char8 idText[33] = {};
    uid.toString(idText);
    const std::string comp = "component state", cont = "controller state";

    SECTION("written by the SDK, with an Info chunk: read by mini-daw")
    {
        MemoryStream cs, es, out;
        int32 n = 0;
        cs.write(const_cast<char*>(comp.data()), static_cast<int32>(comp.size()), &n);
        es.write(const_cast<char*>(cont.data()), static_cast<int32>(cont.size()), &n);
        cs.seek(0, IBStream::kIBSeekSet, nullptr);
        es.seek(0, IBStream::kIBSeekSet, nullptr);
        const char* xml = "<?xml version=\"1.0\"?><MetaInfo/>";
        REQUIRE(PresetFile::savePreset(&out, uid, &cs, &es, xml));
        std::string err;
        const auto d = decodeVstPreset(std::string(out.getData(), static_cast<std::size_t>(out.getSize())), err);
        REQUIRE(d);
        INFO(err);
        CHECK(d->classId == std::string(idText, 32));
        CHECK(d->component == comp);
        CHECK(d->controller == cont);
    }
    SECTION("written by mini-daw: read by the SDK")
    {
        VstPresetData d;
        d.classId = std::string(idText, 32);
        d.component = comp;
        d.controller = cont;
        const std::string bytes = encodeVstPreset(d);
        MemoryStream in;
        int32 n = 0;
        in.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &n);
        in.seek(0, IBStream::kIBSeekSet, nullptr);
        PresetFile pf(&in);
        REQUIRE(pf.readChunkList());
        CHECK(pf.getClassID() == uid);
        const PresetFile::Entry* e = pf.getEntry(kComponentState);
        REQUIRE(e != nullptr);
        CHECK(bytes.substr(static_cast<std::size_t>(e->offset), static_cast<std::size_t>(e->size)) == comp);
        const PresetFile::Entry* c = pf.getEntry(kControllerState);
        REQUIRE(c != nullptr);
        CHECK(bytes.substr(static_cast<std::size_t>(c->offset), static_cast<std::size_t>(c->size)) == cont);
    }
}
