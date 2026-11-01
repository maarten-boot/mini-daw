#include "minidaw/theme.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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
    {Role::ScrollbarThumb, Role::ScrollbarTrack},
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
    p[Role::ControlHovered]   = hex("#D4D4D4");
    p[Role::ControlActive]    = hex("#BDBDBD");
    p[Role::Selection]        = hex("#C7DCF7");
    p[Role::SelectionHovered] = hex("#B3CEF2");
    p[Role::SelectionActive]  = hex("#9DBFEC");
    p[Role::TableHeader]      = hex("#DDDDDD");
    p[Role::TableRowAlt]      = hex("#F2F2F2");
    p[Role::ScrollbarTrack]   = hex("#F0F0F0");
    p[Role::Text]             = hex("#000000");
    p[Role::TextDisabled]     = hex("#595959");
    p[Role::Border]           = hex("#595959");
    p[Role::Accent]           = hex("#003F8A");
    p[Role::ScrollbarThumb]   = hex("#595959");
    return p;
}

std::span<const ColorPair> colorPairs()
{
    return kPairs;
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

    double value = 0.0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (text.empty() || ec != std::errc() || ptr != last) {
        return {std::nullopt, "'" + std::string(text) +
                                  "' is not a contrast setting. Use AA, AAA, Max or a number from 4.5 to 21."};
    }
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

    EnforceResult result{p, {}};
    for (const ColorPair& pair : kPairs) {
        if (!meetsContrast(contrastRatio(p[pair.foreground], p[pair.background]), minimum)) {
            result.unresolved.push_back(pair);
        }
    }
    return result;
}

} // namespace minidaw
