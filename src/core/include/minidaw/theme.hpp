#pragma once

// Theme palette and contrast enforcement (SPEC-UI-01, 02, 05, 06).
//
// The UI uses a small set of colour roles. Every foreground role is listed
// together with each background it is drawn on (colorPairs()). Contrast is
// checked and enforced on exactly those pairs. The GUI layer maps every
// toolkit colour slot to one of these roles, so no colour escapes the check.

#include "minidaw/color.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

enum class Role : std::size_t {
    // Backgrounds
    Background,        // window background
    Control,           // buttons, input fields, checkboxes, title bars of inactive windows
    ControlHovered,
    ControlActive,
    Selection,         // selected rows/tabs, active window title, text selection
    SelectionHovered,
    SelectionActive,
    TableHeader,
    TableRowAlt,       // every other table row
    ScrollbarTrack,
    // Foregrounds
    Text,
    TextDisabled,
    Border,            // window/frame borders, separators, table lines
    Accent,            // check marks, slider handles, focus cursor, links
    ScrollbarThumb,
    Count
};

inline constexpr std::size_t kRoleCount = static_cast<std::size_t>(Role::Count);

std::string_view roleName(Role role);

struct Palette {
    std::array<Rgb, kRoleCount> colors{};
    Rgb& operator[](Role r) { return colors[static_cast<std::size_t>(r)]; }
    const Rgb& operator[](Role r) const { return colors[static_cast<std::size_t>(r)]; }
};

// Default light theme: white background, black text, every pair >= 4.5:1.
Palette defaultPalette();

struct ColorPair {
    Role foreground;
    Role background;
};

// All foreground/background combinations that appear on screen.
std::span<const ColorPair> colorPairs();

// Lowest contrast ratio over all pairs, and which pair it is.
struct LowestPair {
    ColorPair pair;
    double ratio;
};
LowestPair lowestContrast(const Palette& palette);

// --- Contrast presets (SPEC-UI-05) -----------------------------------------

enum class ContrastPreset { AA, AAA, Max, Custom };

inline constexpr double kMinContrastAA = 4.5;
inline constexpr double kMinContrastAAA = 7.0;
inline constexpr double kMinContrastMax = 21.0;

struct ContrastSetting {
    ContrastPreset preset = ContrastPreset::AA;
    double custom = kMinContrastAA;  // only used when preset == Custom

    double minimum() const;
    std::string toString() const;    // "AA", "AAA", "Max" or e.g. "10"
    friend bool operator==(const ContrastSetting&, const ContrastSetting&) = default;
};

struct ContrastParseResult {
    std::optional<ContrastSetting> setting;
    std::string error;               // set when setting is empty
};

// Accepts "AA", "AAA", "Max" (case-insensitive) or a number from 4.5 to 21.
// Values below 4.5 are rejected: AA is the floor (SPEC-UI-02).
ContrastParseResult parseContrastSetting(std::string_view text);

// Validates a custom minimum entered in the GUI. Empty string = valid.
std::string validateCustomContrast(double value);

// --- Enforcement (SPEC-UI-06) ----------------------------------------------

struct EnforceResult {
    Palette palette;
    std::vector<ColorPair> unresolved;  // pairs that could not reach the minimum (should be empty)
};

// Adjust the palette so every pair reaches `minimum`. Foregrounds move first,
// as little as possible; a background only moves when its foreground alone
// cannot get there. At 21:1 this yields pure black on pure white.
EnforceResult enforceMinimumContrast(const Palette& palette, double minimum);

} // namespace minidaw
