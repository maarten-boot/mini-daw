#include "minidaw/theme.hpp"
#include "minidaw/number_text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace minidaw {
namespace {

Rgb hex(const char* text)
{
    return parseHex(text).value_or(kBlack);
}

constexpr ColorPair kPairs[] = {
    {Role::Text, Role::Background},
    {Role::Text, Role::Control},
    {Role::Text, Role::ControlHovered},
    {Role::Text, Role::ControlActive},
    {Role::Text, Role::Selection},
    {Role::Text, Role::SelectionHovered},
    {Role::Text, Role::SelectionActive},
    {Role::Text, Role::TableHeader},
    {Role::Text, Role::TableRowAlt},
    {Role::TextDisabled, Role::Background},
    {Role::TextDisabled, Role::Control},
    {Role::TextDisabled, Role::TableRowAlt},
    {Role::Border, Role::Background},
    {Role::Border, Role::TableRowAlt},
    {Role::Accent, Role::Background},
    {Role::Accent, Role::Control},
    {Role::Accent, Role::ControlHovered},
    {Role::Accent, Role::ControlActive},
    // The hover / pressed frame and the selection bar (PL1) are drawn in Accent
    // on every background a control or list item can have.
    {Role::Accent, Role::Selection},
    {Role::Accent, Role::SelectionHovered},
    {Role::Accent, Role::SelectionActive},
    {Role::Accent, Role::TableHeader},
    {Role::Accent, Role::TableRowAlt},
    // A list entry that is selected but disabled ("Sound from: this recording"
    // without a take) shows these on Selection (crosscheck #15, W2).
    {Role::TextDisabled, Role::Selection},
    {Role::Border, Role::Selection},
    {Role::ScrollbarThumb, Role::ScrollbarTrack},
};

// A disabled control is flat: what shows behind it is the window, a table
// row or (for a disabled control drawn on a button-like frame) Control.
// Each one is also a normal pair, so enforcement lifts it at full colour.
constexpr ColorPair kDisabledPairs[] = {
    {Role::Text, Role::Background},          {Role::Text, Role::TableRowAlt},
    {Role::Text, Role::Control},             {Role::TextDisabled, Role::Background},
    {Role::TextDisabled, Role::TableRowAlt}, {Role::TextDisabled, Role::Control},
    {Role::Border, Role::Background},        {Role::Border, Role::TableRowAlt},
    {Role::Accent, Role::Background},
    {Role::Accent, Role::TableRowAlt},       {Role::Accent, Role::Control},
    {Role::Text, Role::Selection},           {Role::TextDisabled, Role::Selection},
    {Role::Border, Role::Selection},
};

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

} // namespace

std::string_view roleName(Role role)
{
    switch (role) {
    case Role::Background:       return "Background";
    case Role::Control:          return "Control";
    case Role::ControlHovered:   return "Control (hovered)";
    case Role::ControlActive:    return "Control (pressed)";
    case Role::Selection:        return "Selection";
    case Role::SelectionHovered: return "Selection (hovered)";
    case Role::SelectionActive:  return "Selection (pressed)";
    case Role::TableHeader:      return "Table header";
    case Role::TableRowAlt:      return "Table row (alternate)";
    case Role::ScrollbarTrack:   return "Scrollbar track";
    case Role::Text:             return "Text";
    case Role::TextDisabled:     return "Text (disabled)";
    case Role::Border:           return "Border";
    case Role::Accent:           return "Accent";
    case Role::ScrollbarThumb:   return "Scrollbar thumb";
    case Role::Count:            break;
    }
    return "?";
}

Palette defaultPalette()
{
    Palette p;
    p[Role::Background]       = hex("#FFFFFF");
    p[Role::Control]          = hex("#E8E8E8");
    p[Role::ControlHovered]   = hex("#CCCCCC");  // PL1: 1.3:1 from Control, plus the hover frame
    p[Role::ControlActive]    = hex("#A6A6A6");  // PL1: 2:1 from Control, so a press is seen
    p[Role::Selection]        = hex("#C7DCF7");
    p[Role::SelectionHovered] = hex("#B3CEF2");
    p[Role::SelectionActive]  = hex("#9DBFEC");
    p[Role::TableHeader]      = hex("#DDDDDD");
    p[Role::TableRowAlt]      = hex("#F2F2F2");
    p[Role::ScrollbarTrack]   = hex("#F0F0F0");
    p[Role::Text]             = hex("#000000");
    p[Role::TextDisabled]     = hex("#595959");
    p[Role::Border]           = hex("#595959");
    p[Role::Accent]           = hex("#00336F");  // darker than before (#003F8A) for the darker pressed colour
    p[Role::ScrollbarThumb]   = hex("#595959");
    return p;
}

std::span<const ColorPair> colorPairs()
{
    return kPairs;
}

std::span<const ColorPair> disabledPairs()
{
    return kDisabledPairs;
}

double disabledOpacity(const Palette& palette, double minimum)
{
    double opacity = kMinDisabledOpacity;
    for (const ColorPair& pair : kDisabledPairs) {
        opacity = std::max(opacity, minimumOpacity(palette[pair.foreground], palette[pair.background], minimum));
    }
    return std::min(opacity, 1.0);
}

LowestPair lowestContrast(const Palette& palette)
{
    LowestPair lowest{kPairs[0], 1e9};
    for (const ColorPair& pair : kPairs) {
        const double r = contrastRatio(palette[pair.foreground], palette[pair.background]);
        if (r < lowest.ratio) {
            lowest = {pair, r};
        }
    }
    return lowest;
}

double ContrastSetting::minimum() const
{
    switch (preset) {
    case ContrastPreset::AA:     return kMinContrastAA;
    case ContrastPreset::AAA:    return kMinContrastAAA;
    case ContrastPreset::Max:    return kMinContrastMax;
    case ContrastPreset::Custom: return custom;
    }
    return kMinContrastAA;
}

std::string ContrastSetting::toString() const
{
    switch (preset) {
    case ContrastPreset::AA:  return "AA";
    case ContrastPreset::AAA: return "AAA";
    case ContrastPreset::Max: return "Max";
    case ContrastPreset::Custom: break;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", custom);
    return buf;
}

std::string validateCustomContrast(double value)
{
    char buf[160];
    if (!(value >= kMinContrastAA)) {  // also catches NaN
        std::snprintf(buf, sizeof buf,
                      "%g:1 is below the minimum of 4.5:1 (WCAG AA). Contrast can only be raised.", value);
        return buf;
    }
    if (value > kMinContrastMax) {
        std::snprintf(buf, sizeof buf, "%g:1 is above the highest possible contrast of 21:1.", value);
        return buf;
    }
    return {};
}

ContrastParseResult parseContrastSetting(std::string_view text)
{
    text = trim(text);
    const std::string key = lower(text);
    if (key == "aa")  return {ContrastSetting{ContrastPreset::AA, kMinContrastAA}, {}};
    if (key == "aaa") return {ContrastSetting{ContrastPreset::AAA, kMinContrastAA}, {}};
    if (key == "max") return {ContrastSetting{ContrastPreset::Max, kMinContrastAA}, {}};

    const std::optional<double> parsed = parseDouble(text);
    if (!parsed) {
        return {std::nullopt, "'" + std::string(text) +
                                  "' is not a contrast setting. Use AA, AAA, Max or a number from 4.5 to 21."};
    }
    const double value = *parsed;
    if (std::string err = validateCustomContrast(value); !err.empty()) {
        return {std::nullopt, err};
    }
    return {ContrastSetting{ContrastPreset::Custom, value}, {}};
}

EnforceResult enforceMinimumContrast(const Palette& input, double minimum)
{
    Palette p = input;

    // A few passes, because moving a background can affect another pair that
    // shares it. With a light theme (dark foregrounds on light backgrounds)
    // every move only increases contrast, so one pass normally suffices.
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (const ColorPair& pair : kPairs) {
            Rgb& fg = p[pair.foreground];
            Rgb& bg = p[pair.background];
            if (meetsContrast(contrastRatio(fg, bg), minimum)) {
                continue;
            }
            changed = true;
            if (auto adj = adjustToContrast(fg, bg, minimum); adj.ok) {
                fg = adj.color;
                continue;
            }
            // The foreground alone cannot get there: push the background away
            // from the foreground's extreme, then fit the foreground again.
            const Rgb extreme = relativeLuminance(fg) <= relativeLuminance(bg) ? kBlack : kWhite;
            bg = adjustToContrast(bg, extreme, minimum).color;
            fg = adjustToContrast(fg, bg, minimum).color;
        }
        if (!changed) {
            break;
        }
    }

    EnforceResult result{p, {}, disabledOpacity(p, minimum)};
    for (const ColorPair& pair : kPairs) {
        if (!meetsContrast(contrastRatio(p[pair.foreground], p[pair.background]), minimum)) {
            result.unresolved.push_back(pair);
        }
    }
    return result;
}

namespace {

// The palette of an area before enforcement: tints of `background`, dark or
// light text.
Palette areaRoles(const Rgb& background, bool dark)
{
    Palette p = defaultPalette();
    p[Role::Background] = background;
    if (!dark) {
        p[Role::Control]          = mix(background, kBlack, 0.09);
        p[Role::ControlHovered]   = mix(background, kBlack, 0.20);
        p[Role::ControlActive]    = mix(background, kBlack, 0.35);
        p[Role::TableHeader]      = mix(background, kBlack, 0.13);
        p[Role::TableRowAlt]      = mix(background, kBlack, 0.05);
        p[Role::ScrollbarTrack]   = mix(background, kBlack, 0.06);
        p[Role::Selection]        = mix(background, hex("#2F6FD0"), 0.25);
        p[Role::SelectionHovered] = mix(background, hex("#2F6FD0"), 0.32);
        p[Role::SelectionActive]  = mix(background, hex("#2F6FD0"), 0.40);
    } else {
        p[Role::Control]          = mix(background, kWhite, 0.12);
        p[Role::ControlHovered]   = mix(background, kWhite, 0.24);
        p[Role::ControlActive]    = mix(background, kWhite, 0.36);
        p[Role::TableHeader]      = mix(background, kWhite, 0.16);
        p[Role::TableRowAlt]      = mix(background, kWhite, 0.06);
        p[Role::ScrollbarTrack]   = mix(background, kWhite, 0.05);
        p[Role::Selection]        = mix(background, hex("#5B9BF0"), 0.35);
        p[Role::SelectionHovered] = mix(background, hex("#5B9BF0"), 0.45);
        p[Role::SelectionActive]  = mix(background, hex("#5B9BF0"), 0.55);
        p[Role::Text]             = kWhite;
        p[Role::TextDisabled]     = hex("#B5B5B5");
        p[Role::Border]           = hex("#B5B5B5");
        p[Role::Accent]           = hex("#A8CCFF");
        p[Role::ScrollbarThumb]   = hex("#B5B5B5");
    }
    return p;
}

bool allPairsMeet(const EnforceResult& r, double minimum)
{
    if (!r.unresolved.empty()) return false;
    for (const auto& pair : colorPairs()) {
        if (!meetsContrast(contrastRatio(r.palette[pair.foreground], r.palette[pair.background]), minimum)) return false;
    }
    return true;
}

} // namespace

EnforceResult areaPalette(const Rgb& background, double minimum)
{
    // Dark or light: whichever text colour has more contrast on it.
    const bool dark = contrastRatio(kWhite, background) > contrastRatio(kBlack, background);
    // A middle colour leaves no room for the tinted controls: it moves towards
    // white (dark text) or black (light text) until every pair reaches the
    // minimum. At white / black this is the main palette's case, which does.
    const Rgb end = dark ? kBlack : kWhite;
    EnforceResult r;
    // Enforcing at a stricter level sometimes succeeds where the level itself
    // does not (fixing one pair can undo another), so those are tried first,
    // before the background moves further.
    for (int step = 0; step <= 20; ++step) {
        const Palette roles = areaRoles(mix(background, end, step / 20.0), dark);
        for (const double target : {minimum, kMinContrastAAA, 10.0, 14.0}) {
            if (target < minimum) continue;
            r = enforceMinimumContrast(roles, target);
            if (allPairsMeet(r, minimum)) return r;
        }
    }
    return r;
}

} // namespace minidaw
