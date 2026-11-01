#pragma once

// Application configuration file (SPEC-UI-04; device settings follow in P4).
//
// Format: a small INI file, for example
//
//   [ui]
//   ; AA (4.5), AAA (7), Max (21) or a number from 4.5 to 21
//   min_contrast = AAA
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

struct AppConfig {
    ContrastSetting contrast;
};

struct ConfigLoadResult {
    AppConfig config;                  // defaults for anything missing or invalid
    std::vector<std::string> warnings; // e.g. "config.ini line 3: ..."
};

// Parse config text. Never fails: problems become warnings and defaults are kept.
ConfigLoadResult parseConfig(std::string_view text, std::string_view sourceName = "config");

// Return `text` with `key` in `[section]` set to `value`. Every other line,
// including comments, is kept. The key or section is added if missing.
std::string setConfigValue(std::string_view text, std::string_view section, std::string_view key,
                           std::string_view value);

// Text written when no config file exists yet.
std::string defaultConfigText();

std::filesystem::path defaultConfigPath();

// Missing file = defaults without warnings.
ConfigLoadResult loadConfigFile(const std::filesystem::path& path);

// Store the contrast setting, keeping the rest of the file. Creates the file
// and its folder if needed. Returns an error message, or empty on success.
std::string saveContrastSetting(const std::filesystem::path& path, const ContrastSetting& setting);

} // namespace minidaw
