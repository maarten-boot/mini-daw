#pragma once
// Decimal numbers as text, the same on every system and in every locale
// (P34a). std::to_chars / std::from_chars for double where the standard
// library has them; Apple's libc++ has no floating-point from_chars and its
// to_chars needs macOS 13.3, so there a portable fallback does the same.

#include <optional>
#include <string>
#include <string_view>

namespace minidaw {

// The shortest text that reads back as exactly this value, like
// std::to_chars(first, last, value): "0.5", "120", "1e+20", "-2.5e-07".
std::string formatDouble(double value);

// The whole text as a number, like std::from_chars: no spaces, no '+', no
// hex; nullopt when it is not a number, is "inf" / "nan", or does not fit
// in a double.
std::optional<double> parseDouble(std::string_view text);

namespace detail {
// The fallbacks, always built so the tests can compare them with the
// standard library where it has both (T-MAC-05).
std::string formatDoubleFallback(double value);
std::optional<double> parseDoubleFallback(std::string_view text);
} // namespace detail

} // namespace minidaw
