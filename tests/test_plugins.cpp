// VST3 hosting: finding and scanning modules (SPEC-VST-01).
// Uses the test synth built with the tests (MINIDAW_TEST_VST3).

#include "minidaw/path_util.hpp"
#include "minidaw/plugin_scan.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

const fs::path kTestSynth = fs::path(MINIDAW_TEST_VST3);

} // namespace

TEST_CASE("T-VST-08: VST3 modules are found in the folders", "[vst][T-VST-08]")
{
    test::TempDir dir;
    const fs::path a = dir.path() / "VST3";
    fs::create_directories(a / "Vendor" / "Synth A.vst3" / "Contents" / "x86_64-win");
    std::ofstream(a / "Vendor" / "Synth A.vst3" / "Contents" / "x86_64-win" / "Synth A.vst3") << "x";  // inside a bundle
    fs::create_directories(a / "Old");
    std::ofstream(a / "Old" / "Single.vst3") << "x";   // a module as one file
    std::ofstream(a / "readme.txt") << "x";
    const auto found = findVst3Modules({a, dir.path() / "missing"});
    REQUIRE(found.size() == 2);
    CHECK(found[0].filename() == "Single.vst3");
    CHECK(found[1].filename() == "Synth A.vst3");
    CHECK_FALSE(standardVst3Folders().empty());
}

TEST_CASE("T-VST-09: scanning the test synth finds one instrument", "[vst][T-VST-09]")
{
    REQUIRE(fs::exists(kTestSynth));
    const ScannedModule m = scanModule(kTestSynth);
    INFO(m.error);
    REQUIRE(m.status == ModuleStatus::Ok);
    CHECK_FALSE(m.fromModuleInfo);
    REQUIRE(m.classes.size() == 1);  // the controller is not an audio processor
    const PluginClass& c = m.classes[0];
    CHECK(c.name == "mini-daw Test Synth");
    CHECK(c.vendor == "mini-daw tests");
    CHECK(c.subCategories == "Instrument|Synth");
    CHECK(c.instrument);
    CHECK(c.classId.size() == 32);
    CHECK(m.stamp != 0);
}

TEST_CASE("T-VST-10: moduleinfo.json is used without loading; damaged modules fail cleanly", "[vst][T-VST-10]")
{
    test::TempDir dir;
    // A bundle whose binary is junk but which has a moduleinfo.json: listed without loading.
    const fs::path b = dir.path() / "Info.vst3";
    fs::create_directories(b / "Contents" / "Resources");
    fs::create_directories(b / "Contents" / "x86_64-linux");
    fs::create_directories(b / "Contents" / "x86_64-win");
    std::ofstream(b / "Contents" / "x86_64-linux" / "Info.so") << "not a library";
    std::ofstream(b / "Contents" / "x86_64-win" / "Info.vst3") << "not a library";
    std::ofstream(b / "Contents" / "Resources" / "moduleinfo.json") << R"({
      "Name": "Info", "Version": "1.0", "Factory Info": {"Vendor": "V", "URL": "", "E-Mail": "",
        "Flags": {"Unicode": true, "Classes Discardable": false, "Component Non Discardable": false}},
      "Compatibility": [],
      "Classes": [
        {"CID": "0123456789ABCDEF0123456789ABCDEF", "Category": "Audio Module Class", "Name": "Fake Synth",
         "Vendor": "V", "Version": "1.0", "SDKVersion": "VST 3.8", "Sub Categories": ["Instrument", "Synth"],
         "Class Flags": 0, "Cardinality": 2147483647, "Snapshots": []},
        {"CID": "11111111111111111111111111111111", "Category": "Audio Module Class", "Name": "Fake Delay",
         "Vendor": "V", "Version": "1.0", "SDKVersion": "VST 3.8", "Sub Categories": ["Fx", "Delay"],
         "Class Flags": 0, "Cardinality": 2147483647, "Snapshots": []}]})";
    const ScannedModule m = scanModule(b);
    INFO(m.error);
    REQUIRE(m.status == ModuleStatus::Ok);
    CHECK(m.fromModuleInfo);
    REQUIRE(m.classes.size() == 2);
    CHECK(m.classes[0].name == "Fake Synth");
    CHECK(m.classes[0].instrument);
    CHECK_FALSE(m.classes[1].instrument);  // an effect

    // A module that is not a library: failed, with a reason, no crash.
    std::ofstream(dir.path() / "Junk.vst3") << "not a library";
    const ScannedModule j = scanModule(dir.path() / "Junk.vst3");
    CHECK(j.status == ModuleStatus::Failed);
    CHECK_FALSE(j.error.empty());
}

TEST_CASE("T-VST-11: plugin cache and scanner (reuse, crash marker)", "[vst][T-VST-11]")
{
    test::TempDir dir;
    const fs::path folder = dir.path() / "VST3";
    fs::create_directories(folder);
    fs::copy(kTestSynth, folder / "minidaw-testsynth.vst3", fs::copy_options::recursive);
    std::ofstream(folder / "Bad.vst3") << "not a library";
    const fs::path cacheFile = dir.path() / "plugins.json";
    const fs::path marker = dir.path() / "plugin-scan.inprogress";

    PluginScanner scan;
    scan.start({folder}, {}, marker);
    CHECK(scan.total() == 2);
    while (scan.step()) {
    }
    CHECK_FALSE(fs::exists(marker));
    PluginCache cache = scan.result();
    REQUIRE(cache.modules.size() == 2);
    const auto instruments = cache.instruments();
    REQUIRE(instruments.size() == 1);
    CHECK(cache.findClass(instruments[0].classId) != nullptr);
    std::string err;
    REQUIRE(savePluginCache(cacheFile, cache, err));
    PluginCache back = loadPluginCache(cacheFile);
    REQUIRE(back.modules.size() == 2);
    CHECK(back.instruments().size() == 1);
    CHECK(back.modules[0].status == ModuleStatus::Failed);  // Bad.vst3 (sorted first)

    // Unchanged modules are taken from the cache (here: a changed name proves it).
    back.modules[1].classes[0].name = "from the cache";
    scan.start({folder}, back, marker);
    while (scan.step()) {
    }
    CHECK(scan.result().instruments()[0].name == "from the cache");

    // A module that crashed mini-daw during the last scan: marked and skipped.
    std::ofstream(marker) << pathToUtf8(folder / "minidaw-testsynth.vst3") << "\n";
    scan.start({folder}, back, marker);
    CHECK(scan.crashedLastTime() == pathToUtf8(folder / "minidaw-testsynth.vst3"));
    while (scan.step()) {
    }
    CHECK(scan.result().instruments().empty());
    const auto& crashed = scan.result().modules[1];
    CHECK(crashed.status == ModuleStatus::Crashed);
    // ... until the user asks to try again.
    PluginCache withCrash = scan.result();
    scan.start({folder}, withCrash, marker, /*retryCrashed=*/true);
    while (scan.step()) {
    }
    CHECK(scan.result().instruments().size() == 1);

    // A damaged cache file: empty, no crash.
    std::ofstream(cacheFile) << "{ not json";
    CHECK(loadPluginCache(cacheFile).modules.empty());
}
