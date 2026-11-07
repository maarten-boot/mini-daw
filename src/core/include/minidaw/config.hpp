#pragma once

// Application configuration file (SPEC-UI-04, SPEC-UI-08, SPEC-DEV-03, SPEC-REC-05a, SPEC-REC-04b, SPEC-REC-07).
//
// Format: a small INI file, for example
//
//   [ui]
//   min_contrast = AAA        ; AA (4.5), AAA (7), Max (21) or 4.5 .. 21
//   font_size = 18            ; text size in pixels, 12 .. 32
//
//   [audio]
//   input_device = "Line In (High Definition Audio Device)"   ; or (default) / (none)
//   output_device = (default)
//   sample_rate = 48000
//   buffer_size = 256
//
//   [recording]
//   sync_to_disk = true       ; force take files to disk each second (true/false)
//   folder = "D:\Takes"         ; empty = Documents\mini-daw (Windows), ~/Music/mini-daw
//                               ; network path: folder = \\server\takes (no quotes)
//   midi_timing = ppq         ; ppq (960 PPQ, 120 BPM) or smpte (1 ms ticks)
//   latency_correction_ms = 0 ; overdub: shift new audio takes earlier by this much more
//   auto_pause_seconds = 5    ; auto record: a part ends after this many quiet seconds (1 .. 60)
//   auto_threshold_db = -40   ; auto record: an audio input counts as playing from here (-60 .. -20)
//
//   [window]
//   remember = true           ; reopen at the size and place it had when closed
//   x = 100                   ; written by mini-daw: the normal (not maximised)
//   y = 80                    ;   position and size in screen pixels
//   width = 1600
//   height = 950
//   maximized = false
//
//   [project]
//   last = "D:\Music\mini-daw\projects\Jam"   ; reopened at start; empty = none
//   recent1 = "..." ... recent8                 ; File > Recent projects
//
// Default location:
//   Windows: %APPDATA%\mini-daw\config.ini
//   macOS:   ~/Library/Application Support/mini-daw/config.ini
//   Linux:   $XDG_CONFIG_HOME/mini-daw/config.ini (default ~/.config/...)
// The command-line option --config <file> overrides it.

#include "minidaw/smf.hpp"
#include "minidaw/theme.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

inline constexpr float kDefaultFontSize = 16.0f;
inline constexpr float kMinFontSize = 12.0f;
inline constexpr float kMaxFontSize = 32.0f;

// Special device names in the config file.
inline constexpr const char* kDefaultDevice = "(default)";  // the system's default device
inline constexpr const char* kNoDevice = "(none)";          // do not open this direction

inline constexpr unsigned int kDefaultSampleRate = 48000;
inline constexpr unsigned int kDefaultBufferFrames = 256;

struct AudioSettings {
    std::string inputDevice = kDefaultDevice;
    std::string outputDevice = kDefaultDevice;
    unsigned int sampleRate = kDefaultSampleRate;
    unsigned int bufferFrames = kDefaultBufferFrames;
    friend bool operator==(const AudioSettings&, const AudioSettings&) = default;
};

struct RecordingSettings {
    // Force take files to the disk (not just the OS cache) at each header
    // refresh, so a take survives power loss (SPEC-REC-05a). Can be switched
    // off for slow disks.
    bool syncToDisk = true;
    std::string folder;                     // empty = defaultRecordingFolder()
    MidiTiming midiTiming = MidiTiming::Ppq;
    // Overdub (SPEC-PLAY-05): added to the estimated round-trip latency by
    // which a new audio take is moved earlier. Whole ms, -500..500.
    int latencyCorrectionMs = 0;
    // Auto record (SPEC-REC-08): a part ends after this many quiet seconds
    // (1..60); an audio input counts as playing from this level (-60..-20 dBFS).
    int autoPauseSeconds = 5;
    int autoThresholdDb = -40;
    friend bool operator==(const RecordingSettings&, const RecordingSettings&) = default;
};

// Documents\mini-daw on Windows, ~/Music/mini-daw on Linux and macOS (SPEC-REC-07).
std::filesystem::path defaultRecordingFolder();

// The folder from the settings, or the default when it is empty.
std::filesystem::path recordingFolder(const RecordingSettings& settings);

// Projects (SPEC-PRJ-02a): the project open when mini-daw last closed, and
// the recently used ones (newest first, at most kMaxRecentProjects).
inline constexpr std::size_t kMaxRecentProjects = 8;
struct ProjectSettings {
    std::string last;                  // folder (UTF-8); empty = none
    std::vector<std::string> recent;
    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};
// Put `folder` first in the recent list (no duplicates, at most kMaxRecentProjects).
void rememberProject(ProjectSettings& settings, const std::string& folder);

// VST3 instruments (SPEC-VST-01a): folders searched besides the standard ones.
struct InstrumentSettings {
    std::vector<std::string> folders;  // UTF-8, at most 8 (folder1 .. folder8)
    friend bool operator==(const InstrumentSettings&, const InstrumentSettings&) = default;
};

// Main window place and size (SPEC-UI-09), written when mini-daw closes.
struct WindowSettings {
    bool remember = true;
    bool hasGeometry = false;  // x, y, width and height were all read
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
};

// A rectangle in screen pixels (a window, or a monitor's work area).
struct ScreenRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    friend bool operator==(const ScreenRect&, const ScreenRect&) = default;
};

// Where the main window goes at start (SPEC-UI-09): the saved rectangle, made
// to fit the work area it overlaps most (smaller if needed, moved inside).
// Nothing (use the default place) if remembering is off, nothing was saved,
// or less than 100 x 50 pixels of it would be on any screen (a monitor that
// is gone).
std::optional<ScreenRect> restoredWindowRect(const WindowSettings& saved, const std::vector<ScreenRect>& workAreas);

// Main window tabs (SPEC-UI-12): their order and the one shown at start.
// Ids: "recording", "timeline", "audio" (Audio setup).
std::vector<std::string> defaultTabOrder();
// Every known id once, in the order given; unknown ids and repeats left out,
// missing ones added at the end in the default order.
std::vector<std::string> normalizeTabOrder(const std::vector<std::string>& order);
bool isTabId(std::string_view id);

struct TabSettings {
    std::vector<std::string> order = defaultTabOrder();
    std::string last = "recording";  // shown at start: the tab used last
};

struct AppConfig {
    ContrastSetting contrast;
    float fontSize = kDefaultFontSize;
    WindowSettings window;
    TabSettings tabs;
    AudioSettings audio;
    RecordingSettings recording;
    ProjectSettings project;
    InstrumentSettings instruments;
};

struct ConfigLoadResult {
    AppConfig config;                  // defaults for anything missing or invalid
    std::vector<std::string> warnings; // e.g. "config.ini line 3: ..."
};

// Parse config text. Never fails: problems become warnings and defaults are kept.
ConfigLoadResult parseConfig(std::string_view text, std::string_view sourceName = "config");

// Return `text` with `key` in `[section]` set to `value` (written as-is).
// Every other line, including comments, is kept. The key or section is added if missing.
std::string setConfigValue(std::string_view text, std::string_view section, std::string_view key,
                           std::string_view value);

// Quote a string value for the config file: "..." with \" and \\ escaped.
std::string quoteConfigString(std::string_view value);

// Text written when no config file exists yet.
std::string defaultConfigText();

std::filesystem::path defaultConfigPath();

// Missing file = defaults without warnings.
ConfigLoadResult loadConfigFile(const std::filesystem::path& path);

// Store all settings, keeping comments and unknown lines in the file.
// Creates the file and its folder if needed. Returns an error message, or empty on success.
std::string saveConfigFile(const std::filesystem::path& path, const AppConfig& config);

} // namespace minidaw
