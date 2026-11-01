#pragma once

// Application configuration file (SPEC-UI-04, SPEC-UI-08, SPEC-DEV-03).
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
// Default location:
//   Windows: %APPDATA%\mini-daw\config.ini
//   macOS:   ~/Library/Application Support/mini-daw/config.ini
//   Linux:   $XDG_CONFIG_HOME/mini-daw/config.ini (default ~/.config/...)
// The command-line option --config <file> overrides it.

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

struct AppConfig {
    ContrastSetting contrast;
    float fontSize = kDefaultFontSize;
    AudioSettings audio;
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
