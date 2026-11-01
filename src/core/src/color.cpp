#include "minidaw/color.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace minidaw {
namespace {

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// sRGB channel to linear light. The WCAG 2.x text uses 0.03928 as the
// threshold, the sRGB standard 0.04045; no 8-bit value lies between them,
// so both give identical results for real colours.
double toLinear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

int to8bit(double c)
{
    return static_cast<int>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0));
}

} // namespace

std::optional<Rgb> parseHex(std::string_view text)
{
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return std::nullopt;
    }
    double channel[3] = {};
    for (int i = 0; i < 3; ++i) {
        const int hi = hexDigit(text[static_cast<size_t>(2 * i)]);
        const int lo = hexDigit(text[static_cast<size_t>(2 * i + 1)]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        channel[i] = (hi * 16 + lo) / 255.0;
    }
    return Rgb{channel[0], channel[1], channel[2]};
}

std::string toHex(const Rgb& c)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", to8bit(c.r), to8bit(c.g), to8bit(c.b));
    return buf;
}

double relativeLuminance(const Rgb& c)
{
    return 0.2126 * toLinear(c.r) + 0.7152 * toLinear(c.g) + 0.0722 * toLinear(c.b);
}

double contrastRatio(const Rgb& a, const Rgb& b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

bool meetsContrast(double ratio, double minimum)
{
    return ratio >= minimum - 1e-9;
}

Rgb mix(const Rgb& a, const Rgb& b, double t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

ContrastAdjustment adjustToContrast(const Rgb& color, const Rgb& against, double minimum)
{
    if (meetsContrast(contrastRatio(color, against), minimum)) {
        return {color, true};
    }

    const double lAgainst = relativeLuminance(against);

    // Try one extreme. "Good" means: on the extreme's side of `against` AND
    // enough contrast. Along the line color -> extreme that predicate is
    // monotonic (once true it stays true), so a binary search finds the
    // smallest change.
    auto tryTowards = [&](const Rgb& extreme) -> std::optional<Rgb> {
        const bool towardsWhite = (extreme == kWhite);
        auto good = [&](double t) {
            const Rgb c = mix(color, extreme, t);
            const double l = relativeLuminance(c);
            const bool rightSide = towardsWhite ? l >= lAgainst : l <= lAgainst;
            return rightSide && meetsContrast(contrastRatio(c, against), minimum);
        };
        if (!good(1.0)) {
            return std::nullopt;
        }
        double lo = 0.0;
        double hi = 1.0;
        for (int i = 0; i < 50; ++i) {
            const double mid = 0.5 * (lo + hi);
            (good(mid) ? hi : lo) = mid;
        }
        return mix(color, extreme, hi);
    };

    const bool darker = relativeLuminance(color) <= lAgainst;
    const Rgb sameSide = darker ? kBlack : kWhite;
    const Rgb otherSide = darker ? kWhite : kBlack;

    if (auto c = tryTowards(sameSide)) return {*c, true};
    if (auto c = tryTowards(otherSide)) return {*c, true};

    // Neither reaches the minimum: return the extreme with the most contrast.
    const Rgb best = contrastRatio(kBlack, against) >= contrastRatio(kWhite, against) ? kBlack : kWhite;
    return {best, false};
}

} // namespace minidaw
