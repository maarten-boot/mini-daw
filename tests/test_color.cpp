#include "minidaw/color.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace minidaw;
using Catch::Approx;

static Rgb hex(const char* s) { return parseHex(s).value(); }

TEST_CASE("T-UI-08: contrast of known colour pairs", "[color][T-UI-08]")
{
    CHECK(contrastRatio(kBlack, kWhite) == Approx(21.0));
    CHECK(contrastRatio(hex("#777777"), kWhite) == Approx(4.48).margin(0.01));
    CHECK(contrastRatio(hex("#767676"), kWhite) == Approx(4.54).margin(0.01));  // smallest grey passing AA
    CHECK(contrastRatio(hex("#595959"), kWhite) == Approx(7.0).margin(0.01));
    CHECK(contrastRatio(kWhite, kWhite) == Approx(1.0));
}

TEST_CASE("Contrast is symmetric", "[color]")
{
    const Rgb a = hex("#123456");
    const Rgb b = hex("#ABCDEF");
    CHECK(contrastRatio(a, b) == Approx(contrastRatio(b, a)));
}

TEST_CASE("Hex parsing and formatting", "[color]")
{
    CHECK(parseHex("#FF8000") == Rgb{1.0, 128.0 / 255.0, 0.0});
    CHECK(parseHex("ff8000").has_value());
    CHECK_FALSE(parseHex("#FF80").has_value());
    CHECK_FALSE(parseHex("#GG0000").has_value());
    CHECK_FALSE(parseHex("").has_value());
    CHECK(toHex(hex("#1a2B3c")) == "#1A2B3C");
}

TEST_CASE("adjustToContrast reaches the minimum with a small change", "[color][T-UI-07]")
{
    const Rgb grey = hex("#777777");  // 4.48:1 on white
    auto adj = adjustToContrast(grey, kWhite, 7.0);
    REQUIRE(adj.ok);
    CHECK(contrastRatio(adj.color, kWhite) >= 7.0 - 1e-9);
    CHECK(contrastRatio(adj.color, kWhite) < 7.05);  // not more than needed
    CHECK(relativeLuminance(adj.color) < relativeLuminance(grey));  // got darker, stayed on its side
}

TEST_CASE("adjustToContrast leaves a passing colour unchanged", "[color]")
{
    auto adj = adjustToContrast(kBlack, kWhite, 7.0);
    CHECK(adj.ok);
    CHECK(adj.color == kBlack);
}

TEST_CASE("adjustToContrast at 21:1 gives pure black on white", "[color]")
{
    auto adj = adjustToContrast(hex("#333333"), kWhite, 21.0);
    REQUIRE(adj.ok);
    CHECK(toHex(adj.color) == "#000000");
}

TEST_CASE("adjustToContrast crosses over when its own side cannot reach the minimum", "[color]")
{
    // On a dark-grey background, a slightly darker grey cannot reach 4.5:1
    // by going to black, so it has to become light.
    const Rgb bg = hex("#404040");
    auto adj = adjustToContrast(hex("#303030"), bg, 4.5);
    REQUIRE(adj.ok);
    CHECK(relativeLuminance(adj.color) > relativeLuminance(bg));
    CHECK(contrastRatio(adj.color, bg) >= 4.5 - 1e-9);
}

TEST_CASE("adjustToContrast reports failure when nothing can reach the minimum", "[color]")
{
    // Mid grey: neither black nor white reaches 21:1 against it.
    auto adj = adjustToContrast(hex("#808080"), hex("#777777"), 21.0);
    CHECK_FALSE(adj.ok);
}

TEST_CASE("T-UI-24: smallest opacity that still reaches a minimum", "[color][T-UI-24]")
{
    // Black faded over white reaches 4.5:1 at about #767676, i.e. 54 % opacity.
    const double a = minimumOpacity(kBlack, kWhite, 4.5);
    CHECK(a > 0.5);
    CHECK(a < 0.56);
    CHECK(meetsContrast(contrastRatio(mix(kWhite, kBlack, a), kWhite), 4.5));
    CHECK_FALSE(meetsContrast(contrastRatio(mix(kWhite, kBlack, a - 0.01), kWhite), 4.5));
    // 21:1 needs the full colour; a pair that cannot reach the minimum gives 1.
    CHECK(minimumOpacity(kBlack, kWhite, 21.0) == 1.0);
    CHECK(minimumOpacity(parseHex("#777777").value(), kWhite, 7.0) == 1.0);
}
