#include "minidaw/config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("minidaw-test-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("T-UI-04: min_contrast = 7.0 in the config file", "[config][T-UI-04]")
{
    auto r = parseConfig("[ui]\nmin_contrast = 7.0\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.minimum() == 7.0);
}

TEST_CASE("Config: presets, comments, case and CRLF", "[config]")
{
    auto r = parseConfig("; comment\r\n# other comment\r\n[UI]\r\nMin_Contrast = max ; inline comment\r\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.preset == ContrastPreset::Max);
}

TEST_CASE("T-UI-05: config value below 4.5 is rejected and AA is kept", "[config][T-UI-05]")
{
    auto r = parseConfig("[ui]\nmin_contrast = 3\n", "test.ini");
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].find("test.ini line 2") != std::string::npos);
    CHECK(r.config.contrast.minimum() == kMinContrastAA);
}

TEST_CASE("Config: unknown keys and bad lines give warnings, not errors", "[config]")
{
    auto r = parseConfig("[ui]\ncolour = red\nnot a setting\n[\n");
    CHECK(r.warnings.size() == 3);
    CHECK(r.config.contrast.minimum() == kMinContrastAA);
}

TEST_CASE("Empty or missing config gives defaults", "[config][T-UI-01]")
{
    CHECK(parseConfig("").config.contrast.minimum() == kMinContrastAA);
    auto r = loadConfigFile("/this/path/does/not/exist/config.ini");
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.minimum() == kMinContrastAA);
}

TEST_CASE("The default config text parses without warnings", "[config]")
{
    auto r = parseConfig(defaultConfigText());
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.preset == ContrastPreset::AA);
}

TEST_CASE("T-REC-15: sync_to_disk in [recording], default on", "[config][T-REC-15]")
{
    CHECK(parseConfig("").config.recording.syncToDisk);
    CHECK(parseConfig(defaultConfigText()).config.recording.syncToDisk);
    CHECK_FALSE(parseConfig("[recording]\nsync_to_disk = false\n").config.recording.syncToDisk);
    CHECK_FALSE(parseConfig("[recording]\nsync_to_disk = Off\n").config.recording.syncToDisk);
    CHECK(parseConfig("[recording]\nsync_to_disk = yes\n").config.recording.syncToDisk);
    auto bad = parseConfig("[recording]\nsync_to_disk = maybe\n");
    CHECK(bad.config.recording.syncToDisk);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("sync_to_disk") != std::string::npos);

    TempDir dir;
    AppConfig cfg;
    cfg.recording.syncToDisk = false;
    REQUIRE(saveConfigFile(dir.path / "config.ini", cfg).empty());
    CHECK_FALSE(loadConfigFile(dir.path / "config.ini").config.recording.syncToDisk);
}

TEST_CASE("T-REC-22: recording folder and MIDI timing in the config", "[config][T-REC-22]")
{
    auto d = parseConfig(defaultConfigText());
    CHECK(d.warnings.empty());
    CHECK(d.config.recording.folder.empty());
    CHECK(d.config.recording.midiTiming == MidiTiming::Ppq);
    CHECK(recordingFolder(d.config.recording) == defaultRecordingFolder());
    CHECK(defaultRecordingFolder().filename() == "mini-daw");

    // A Windows path in quotes keeps its backslashes; \\ and \" still work.
    auto r = parseConfig("[recording]\nfolder = \"D:\\Takes\\Jam\"\nmidi_timing = SMPTE\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.recording.folder == "D:\\Takes\\Jam");
    CHECK(r.config.recording.midiTiming == MidiTiming::Smpte);
    CHECK(parseConfig("[recording]\nfolder = \"a\\\\b \\\"q\\\"\"\n").config.recording.folder == "a\\b \"q\"");

    auto bad = parseConfig("[recording]\nmidi_timing = bpm\n");
    CHECK(bad.config.recording.midiTiming == MidiTiming::Ppq);
    REQUIRE(bad.warnings.size() == 1);
    CHECK(bad.warnings[0].find("midi_timing") != std::string::npos);

    TempDir dir;
    AppConfig cfg;
    cfg.recording.folder = "C:\\Users\\me\\Music \"live\"";
    cfg.recording.midiTiming = MidiTiming::Smpte;
    REQUIRE(saveConfigFile(dir.path / "config.ini", cfg).empty());
    const auto back = loadConfigFile(dir.path / "config.ini");
    CHECK(back.warnings.empty());
    CHECK(back.config.recording.folder == cfg.recording.folder);
    CHECK(back.config.recording.midiTiming == MidiTiming::Smpte);
}

TEST_CASE("setConfigValue keeps comments and other lines", "[config]")
{
    const std::string before = "; my notes\n[ui]\n; keep me\nmin_contrast = AA\n\n[other]\nx = 1\n";
    const std::string after = setConfigValue(before, "ui", "min_contrast", "AAA");
    CHECK(after == "; my notes\n[ui]\n; keep me\nmin_contrast = AAA\n\n[other]\nx = 1\n");
}

TEST_CASE("setConfigValue adds a missing key or section", "[config]")
{
    CHECK(setConfigValue("[ui]\n", "ui", "min_contrast", "Max") == "[ui]\nmin_contrast = Max\n");
    CHECK(setConfigValue("[ui]\n\n[other]\nx = 1\n", "ui", "min_contrast", "Max") ==
          "[ui]\nmin_contrast = Max\n\n[other]\nx = 1\n");
    CHECK(setConfigValue("; header\n", "ui", "min_contrast", "Max") == "; header\n\n[ui]\nmin_contrast = Max\n");
    CHECK(setConfigValue("", "ui", "min_contrast", "AA") == "[ui]\nmin_contrast = AA\n");
}

TEST_CASE("T-UI-03/04: an interactive change is saved and read back", "[config][T-UI-03][T-UI-04]")
{
    TempDir dir;
    const fs::path file = dir.path / "sub" / "config.ini";

    SECTION("new file is created with the default comments")
    {
        AppConfig cfg;
        cfg.contrast = {ContrastPreset::AAA};
        REQUIRE(saveConfigFile(file, cfg).empty());
        const std::string text = readFile(file);
        CHECK(text.find("; mini-daw configuration") != std::string::npos);
        CHECK(loadConfigFile(file).config.contrast.preset == ContrastPreset::AAA);
    }
    SECTION("existing file keeps the user's own comments")
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file) << "; my own note\n[ui]\nmin_contrast = AA\n";
        AppConfig cfg;
        cfg.contrast = {ContrastPreset::Custom, 12.5};
        REQUIRE(saveConfigFile(file, cfg).empty());
        const std::string text = readFile(file);
        CHECK(text.find("; my own note") != std::string::npos);
        CHECK(loadConfigFile(file).config.contrast.minimum() == 12.5);
    }
}

TEST_CASE("T-UI-12: font_size in the config file", "[config][T-UI-12]")
{
    CHECK(parseConfig("[ui]\nfont_size = 20\n").config.fontSize == 20.0f);
    auto low = parseConfig("[ui]\nfont_size = 8\n");
    CHECK(low.warnings.size() == 1);
    CHECK(low.config.fontSize == kDefaultFontSize);
    CHECK(parseConfig("[ui]\nfont_size = big\n").warnings.size() == 1);
}

TEST_CASE("T-DEV-03: audio settings in the config file", "[config][T-DEV-03]")
{
    auto r = parseConfig("[audio]\n"
                         "input_device = \"Line In (High Definition Audio Device)\"  ; my interface\n"
                         "output_device = (none)\n"
                         "sample_rate = 44100\n"
                         "buffer_size = 128\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.audio.inputDevice == "Line In (High Definition Audio Device)");
    CHECK(r.config.audio.outputDevice == kNoDevice);
    CHECK(r.config.audio.sampleRate == 44100);
    CHECK(r.config.audio.bufferFrames == 128);
}

TEST_CASE("Audio settings: bad values give warnings and defaults", "[config]")
{
    auto r = parseConfig("[audio]\nsample_rate = 1000\nbuffer_size = lots\ninput_device = \"unclosed\n");
    CHECK(r.warnings.size() == 3);
    CHECK(r.config.audio.sampleRate == kDefaultSampleRate);
    CHECK(r.config.audio.bufferFrames == kDefaultBufferFrames);
}

TEST_CASE("Device names with quotes and semicolons survive save and load", "[config][T-DEV-03]")
{
    TempDir dir;
    const fs::path file = dir.path / "config.ini";
    AppConfig cfg;
    cfg.fontSize = 22;
    cfg.audio.inputDevice = "Mic \"Pro\"; USB #2";
    cfg.audio.outputDevice = kDefaultDevice;
    cfg.audio.sampleRate = 96000;
    cfg.audio.bufferFrames = 512;
    REQUIRE(saveConfigFile(file, cfg).empty());
    auto r = loadConfigFile(file);
    CHECK(r.warnings.empty());
    CHECK(r.config.fontSize == 22.0f);
    CHECK(r.config.audio == cfg.audio);
}

TEST_CASE("quoteConfigString escapes quotes and backslashes", "[config]")
{
    CHECK(quoteConfigString("a\"b\\c") == "\"a\\\"b\\\\c\"");
}
