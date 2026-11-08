#pragma once

// Colour maths for the contrast system (SPEC-UI-02, SPEC-UI-06).
// Contrast follows WCAG 2.x: ratio = (L1 + 0.05) / (L2 + 0.05), where L is the
// relative luminance of an sRGB colour.

#include <optional>
#include <string>
#include <string_view>

namespace minidaw {

// sRGB colour, each channel 0.0 .. 1.0.
struct Rgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

inline constexpr Rgb kBlack{0.0, 0.0, 0.0};
inline constexpr Rgb kWhite{1.0, 1.0, 1.0};

// "#RRGGBB" or "RRGGBB" (case-insensitive). Returns nullopt if malformed.
std::optional<Rgb> parseHex(std::string_view text);
// "#RRGGBB", channels rounded to the nearest 8-bit value.
std::string toHex(const Rgb& c);

// WCAG relative luminance, 0.0 (black) .. 1.0 (white).
double relativeLuminance(const Rgb& c);
// WCAG contrast ratio, 1.0 .. 21.0. Symmetric in its arguments.
double contrastRatio(const Rgb& a, const Rgb& b);
// True if ratio >= minimum, with a tiny tolerance for floating-point rounding.
bool meetsContrast(double ratio, double minimum);

// Linear blend in sRGB space: t = 0 gives a, t = 1 gives b.
Rgb mix(const Rgb& a, const Rgb& b, double t);

struct ContrastAdjustment {
    Rgb color;        // adjusted colour (unchanged input if it already met the minimum)
    bool ok = false;  // false if even pure black or white cannot reach the minimum
};

// Move `color` towards black or white, as little as possible, until its
// contrast against `against` is at least `minimum`. It keeps its current
// side (darker or lighter than `against`) when that side can reach the
// minimum, and only crosses over when it has to.
ContrastAdjustment adjustToContrast(const Rgb& color, const Rgb& against, double minimum);

// Smallest opacity (0..1) at which `fg`, drawn with that opacity over `bg`
// (as the GUI blends it: mix(bg, fg, opacity)), still reaches `minimum`
// against `bg`. 1.0 when even the full colour does not reach it.
double minimumOpacity(const Rgb& fg, const Rgb& bg, double minimum);

} // namespace minidaw
