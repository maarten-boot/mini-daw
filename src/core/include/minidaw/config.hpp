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
//
// Default location:
//   Windows: %APPDATA%\mini-daw\config.ini
//   macOS:   ~/Library/Application Support/mini-daw/config.ini
//   Linux:   $XDG_CONFIG_HOME/mini-daw/config.ini (default ~/.config/...)
// The command-line option --config <file> overrides it.

#include "minidaw/smf.hpp"
#include "minidaw/theme.hpp"

#include <filesystem>
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
    friend bool operator==(const RecordingSettings&, const RecordingSettings&) = default;
};

// Documents\mini-daw on Windows, ~/Music/mini-daw on Linux and macOS (SPEC-REC-07).
std::filesystem::path defaultRecordingFolder();

// The folder from the settings, or the default when it is empty.
std::filesystem::path recordingFolder(const RecordingSettings& settings);

struct AppConfig {
    ContrastSetting contrast;
    float fontSize = kDefaultFontSize;
    AudioSettings audio;
    RecordingSettings recording;
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
