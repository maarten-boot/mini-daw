#include "minidaw/number_text.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <locale.h>
#else
#include <clocale>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#define MINIDAW_STD_FLOAT_CHARCONV 1
#endif

namespace minidaw {

namespace detail {

std::string formatDoubleFallback(double value)
{
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    if (value == 0.0) return std::signbit(value) ? "-0" : "0";

    // The fewest significant digits that read back as the value.
    std::string sci;
    for (int digits = 1; digits <= 17; ++digits) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(digits - 1);
        out << std::scientific << value;
        sci = out.str();
        if (parseDoubleFallback(sci) == value) break;
    }

    // "-d.ddde+XX": the sign, the digits, the exponent.
    std::string text = sci;
    const bool negative = text[0] == '-';
    if (negative) text.erase(0, 1);
    const std::size_t e = text.find('e');
    std::string mantissa = text.substr(0, e);
    const int exponent = std::stoi(text.substr(e + 1));
    std::string d;
    for (const char c : mantissa) {
        if (c != '.') d += c;
    }
    while (d.size() > 1 && d.back() == '0') d.pop_back();
    const int n = static_cast<int>(d.size());

    // Scientific as to_chars writes it: at least two exponent digits.
    std::string s = d.substr(0, 1);
    if (n > 1) s += "." + d.substr(1);
    const int ax = exponent < 0 ? -exponent : exponent;
    s += exponent < 0 ? "e-" : "e+";
    if (ax < 10) s += '0';
    s += std::to_string(ax);

    // Fixed.
    std::string f;
    if (exponent >= n - 1) {
        // A whole number: to_chars writes its exact digits, not the
        // shortest ones followed by zeros (as long either way).
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out.precision(0);
        out << std::fixed << std::fabs(value);
        f = out.str();
    } else if (exponent >= 0) {
        const auto point = static_cast<std::size_t>(exponent + 1);
        f = d.substr(0, point) + "." + d.substr(point);
    } else {
        f = "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + d;
    }

    // to_chars: the shorter one, fixed when they are as long.
    const std::string& best = f.size() <= s.size() ? f : s;
    return negative ? "-" + best : best;
}

namespace {
// A digit other than 0 in the mantissa (before any exponent): "0e-999" is 0, "1e-999" is too small.
bool digitsNonZero(std::string_view text)
{
    for (const char c : text) {
        if (c == 'e' || c == 'E') break;
        if (c >= '1' && c <= '9') return true;
    }
    return false;
}
} // namespace

std::optional<double> parseDoubleFallback(std::string_view text)
{
    // What from_chars accepts in its general format: an optional '-',
    // digits with one optional '.', an optional exponent.
    if (text.empty() || text.size() > 400) return std::nullopt;
    std::size_t i = 0;
    if (text[i] == '-') ++i;
    std::size_t digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    }
    if (digits == 0) return std::nullopt;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        std::size_t expDigits = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') { ++i; ++expDigits; }
        if (expDigits == 0) return std::nullopt;
    }
    if (i != text.size()) return std::nullopt;

    // strtod in the "C" locale (crosscheck #28 CC6: a stream fails on libc++
    // whenever strtod reports ERANGE, also for denormal numbers, which
    // from_chars returns). Out of range only when it gives 0 or infinity.
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
#if defined(_WIN32)
    static const _locale_t cLocale = _create_locale(LC_ALL, "C");
    const double v = _strtod_l(copy.c_str(), &end, cLocale);
#else
    static const locale_t cLocale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    const double v = strtod_l(copy.c_str(), &end, cLocale);
#endif
    if (end != copy.c_str() + copy.size()) return std::nullopt;
    if (!std::isfinite(v)) return std::nullopt;                 // too large: no double
    if (errno == ERANGE && v == 0.0 && digitsNonZero(text)) return std::nullopt;  // too small
    return v;
}

} // namespace detail

std::string formatDouble(double value)
{
#if defined(MINIDAW_STD_FLOAT_CHARCONV)
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, value);
    return std::string(buf, r.ptr);
#else
    return detail::formatDoubleFallback(value);
#endif
}

std::optional<double> parseDouble(std::string_view text)
{
#if defined(MINIDAW_STD_FLOAT_CHARCONV)
    if (text.empty()) return std::nullopt;
    double v = 0.0;
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, v);
    if (r.ec != std::errc() || r.ptr != end || !std::isfinite(v)) return std::nullopt;
    return v;
#else
    return detail::parseDoubleFallback(text);
#endif
}

} // namespace minidaw
