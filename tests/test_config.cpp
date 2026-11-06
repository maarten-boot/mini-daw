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

TEST_CASE("T-REC-22: hand-written Windows folder paths", "[config][T-REC-22]")
{
    auto folder = [](const std::string& line) {
        auto r = parseConfig("[recording]\n" + line + "\n");
        CHECK(r.warnings.empty());
        return r.config.recording.folder;
    };
    CHECK(folder("folder = \"D:\\Takes\\\"") == "D:\\Takes\\");             // trailing backslash in quotes
    CHECK(folder("folder = \"D:\\Takes\\\"  ; my takes") == "D:\\Takes\\");  // ... with a comment after
    CHECK(folder("folder = \\\\server\\takes") == "\\\\server\\takes");      // network path without quotes
    CHECK(folder("folder = \"\\\\\\\\server\\takes\"") == "\\\\server\\takes");  // in quotes, two backslashes stand for one
    CHECK(folder("folder = D:\\My Takes ; comment") == "D:\\My Takes");

    // Saved by mini-daw and read back: a trailing backslash and a quote survive.
    TempDir dir;
    for (const std::string value : {"D:\\Takes\\", "E:\\a \"b\"\\", "\\\\server\\x"}) {
        AppConfig cfg;
        cfg.recording.folder = value;
        REQUIRE(saveConfigFile(dir.path / "c.ini", cfg).empty());
        const auto back = loadConfigFile(dir.path / "c.ini");
        CHECK(back.warnings.empty());
        CHECK(back.config.recording.folder == value);
    }
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

TEST_CASE("T-PLAY-10: latency_correction_ms in [recording]", "[config][T-PLAY-10]")
{
    CHECK(parseConfig(defaultConfigText()).config.recording.latencyCorrectionMs == 0);
    CHECK(parseConfig("[recording]\nlatency_correction_ms = -12\n").config.recording.latencyCorrectionMs == -12);
    for (const char* bad : {"600", "1.5", "x", ""}) {
        const auto r = parseConfig(std::string("[recording]\nlatency_correction_ms = ") + bad + "\n");
        CHECK(r.config.recording.latencyCorrectionMs == 0);
        CHECK(r.warnings.size() == 1);
    }
    TempDir dir;
    AppConfig cfg;
    cfg.recording.latencyCorrectionMs = 25;
    REQUIRE(saveConfigFile(dir.path / "c.ini", cfg).empty());
    CHECK(loadConfigFile(dir.path / "c.ini").config.recording.latencyCorrectionMs == 25);
}

TEST_CASE("T-UI-14: window place and size in the config; where it goes at start", "[config][T-UI-14]")
{
    // Default: remember on, nothing saved yet.
    {
        const auto r = parseConfig(defaultConfigText());
        CHECK(r.warnings.empty());
        CHECK(r.config.window.remember);
        CHECK_FALSE(r.config.window.hasGeometry);
        CHECK_FALSE(restoredWindowRect(r.config.window, {{0, 0, 1920, 1040}}).has_value());
    }
    // Saved and read back, other lines kept.
    TempDir dir;
    const fs::path file = dir.path / "config.ini";
    AppConfig cfg;
    cfg.fontSize = 20;
    cfg.window = {true, true, -1500, 40, 1400, 900, true};
    REQUIRE(saveConfigFile(file, cfg).empty());
    const auto back = loadConfigFile(file);
    CHECK(back.warnings.empty());
    CHECK(back.config.fontSize == 20.0f);
    const WindowSettings& w = back.config.window;
    CHECK(w.remember);
    CHECK(w.hasGeometry);
    CHECK(w.x == -1500);  // a monitor left of the main one
    CHECK(w.y == 40);
    CHECK(w.width == 1400);
    CHECK(w.height == 900);
    CHECK(w.maximized);
    // remember = false: not used at start, and nothing new is needed to read it.
    {
        const auto r = parseConfig("[window]\nremember = false\nx = 10\ny = 10\nwidth = 800\nheight = 600\n");
        CHECK(r.warnings.empty());
        CHECK_FALSE(r.config.window.remember);
        CHECK_FALSE(restoredWindowRect(r.config.window, {{0, 0, 1920, 1040}}).has_value());
    }
    // Bad or missing values: a warning, default place.
    {
        const auto r = parseConfig("[window]\nx = 10\ny = left\nwidth = 800\nheight = 600\nmaximized = maybe\n");
        CHECK(r.warnings.size() == 2);
        CHECK_FALSE(r.config.window.hasGeometry);
        CHECK_FALSE(r.config.window.maximized);
        CHECK_FALSE(parseConfig("[window]\nx = 10\ny = 10\nwidth = 800\n").config.window.hasGeometry);
        CHECK(parseConfig("[window]\nwidth = 50\n").warnings.size() == 1);
    }

    const std::vector<ScreenRect> twoScreens = {{0, 0, 1920, 1040}, {-1600, 0, 1600, 860}};
    WindowSettings s;
    s.hasGeometry = true;
    // On a screen: unchanged.
    s.x = 100, s.y = 80, s.width = 1600, s.height = 900;
    CHECK(restoredWindowRect(s, twoScreens) == ScreenRect{100, 80, 1600, 900});
    // On the left screen, too big for it: smaller and moved inside.
    s.x = -1500, s.y = 40, s.width = 1700, s.height = 900;
    CHECK(restoredWindowRect(s, twoScreens) == ScreenRect{-1600, 0, 1600, 860});
    // Partly off the right edge: moved back.
    s.x = 1800, s.y = 500, s.width = 800, s.height = 600;
    CHECK(restoredWindowRect(s, twoScreens) == ScreenRect{1120, 440, 800, 600});
    // The left screen is gone: default place.
    s.x = -1500, s.y = 40, s.width = 1400, s.height = 800;
    CHECK_FALSE(restoredWindowRect(s, {{0, 0, 1920, 1040}}).has_value());
    // Only a sliver (60 px) on screen: default place.
    s.x = 1860, s.y = 100;
    CHECK_FALSE(restoredWindowRect(s, {{0, 0, 1920, 1040}}).has_value());
    // No screens reported: default place.
    s.x = 100;
    CHECK_FALSE(restoredWindowRect(s, {}).has_value());
}
