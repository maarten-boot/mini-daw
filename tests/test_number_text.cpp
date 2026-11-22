// Decimal numbers as text (P34a): formatDouble / parseDouble, and the
// fallback used where the standard library lacks floating-point
// to_chars / from_chars (Apple's libc++) gives the same results.

#include "minidaw/number_text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace minidaw;
using minidaw::detail::formatDoubleFallback;
using minidaw::detail::parseDoubleFallback;

TEST_CASE("T-MAC-05: numbers as text, the same with and without std::to_chars / from_chars", "[mac][T-MAC-05]")
{
    SECTION("format: shortest text that reads back")
    {
        CHECK(formatDouble(0.5) == "0.5");
        CHECK(formatDouble(120) == "120");
        CHECK(formatDouble(-2.25) == "-2.25");
        CHECK(formatDouble(1e20) == "1e+20");
        CHECK(formatDouble(0.1) == "0.1");
        CHECK(formatDouble(1.0 / 3.0) == "0.3333333333333333");
        CHECK(formatDoubleFallback(0.5) == "0.5");
        CHECK(formatDoubleFallback(120) == "120");
        CHECK(formatDoubleFallback(100) == "100");
        CHECK(formatDoubleFallback(1e20) == "1e+20");
        CHECK(formatDoubleFallback(0.0) == "0");
        CHECK(formatDoubleFallback(1.0 / 3.0) == "0.3333333333333333");
    }

    SECTION("parse: the whole text, as from_chars")
    {
        using Parse = std::optional<double> (*)(std::string_view);
        for (const Parse parse : {Parse(&parseDouble), Parse(&parseDoubleFallback)}) {
            CHECK(parse("4.5") == 4.5);
            CHECK(parse("-0.25") == -0.25);
            CHECK(parse("21") == 21.0);
            CHECK(parse(".5") == 0.5);
            CHECK(parse("5.") == 5.0);
            CHECK(parse("1e3") == 1000.0);
            CHECK(parse("1E-2") == 0.01);
            CHECK_FALSE(parse(""));
            CHECK_FALSE(parse(" 1"));
            CHECK_FALSE(parse("1 "));
            CHECK_FALSE(parse("+1"));
            CHECK_FALSE(parse("1.5.2"));
            CHECK_FALSE(parse("1e"));
            CHECK_FALSE(parse("e5"));
            CHECK_FALSE(parse("-"));
            CHECK_FALSE(parse("."));
            CHECK_FALSE(parse("0x10"));
            CHECK_FALSE(parse("inf"));
            CHECK_FALSE(parse("nan"));
            CHECK_FALSE(parse("1e999"));
            CHECK_FALSE(parse("4,5"));
            // Denormal numbers are numbers; too small to hold is not (crosscheck #28 CC6).
            CHECK(parse("5e-324") == std::numeric_limits<double>::denorm_min());
            CHECK(parse("2.2250738585072014e-310").has_value());
            CHECK_FALSE(parse("1e-400"));
            CHECK(parse("0e-999") == 0.0);
        }
    }

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    SECTION("the fallback matches the standard library")
    {
        std::vector<double> values = {1.0, -1.0, 0.1, 0.2, 0.3, 1e-7, 1.5e-5, 123456789.0, 1e15, 1e16, 1e17,
                                      9007199254740993.0, 1234.5678, 120.0, 48000.0, 0.000123,
                                      std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
                                      std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::epsilon()};
        std::mt19937_64 rng(34);
        std::uniform_real_distribution<double> unit(-1000.0, 1000.0);
        for (int i = 0; i < 2000; ++i) values.push_back(unit(rng));
        for (int i = 0; i < 2000; ++i) {
            // Any finite double, from its bits.
            std::uint64_t bits = rng();
            double v = 0.0;
            std::memcpy(&v, &bits, sizeof v);
            if (std::isfinite(v)) values.push_back(v);
        }
        for (int i = -30; i <= 30; ++i) values.push_back(std::pow(10.0, i) * 1.25);
        int formatDiffer = 0;
        int parseDiffer = 0;
        for (const double v : values) {
            char buf[64];
            const auto r = std::to_chars(buf, buf + sizeof buf, v);
            const std::string expected(buf, r.ptr);
            if (formatDoubleFallback(v) != expected) {
                ++formatDiffer;
                UNSCOPED_INFO(expected << " formatted as " << formatDoubleFallback(v));
            }
            if (parseDoubleFallback(expected) != parseDouble(expected)) {
                ++parseDiffer;
                UNSCOPED_INFO(expected << " parsed differently");
            }
        }
        CHECK(formatDiffer == 0);
        CHECK(parseDiffer == 0);
    }
#endif
}
