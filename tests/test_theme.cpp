#include "minidaw/theme.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <set>

using namespace minidaw;

namespace {

void requireAllPairsAtLeast(const Palette& p, double minimum)
{
    for (const auto& pair : colorPairs()) {
        INFO(roleName(pair.foreground) << " on " << roleName(pair.background) << ": "
             << contrastRatio(p[pair.foreground], p[pair.background]) << ":1");
        CHECK(meetsContrast(contrastRatio(p[pair.foreground], p[pair.background]), minimum));
    }
}

} // namespace

TEST_CASE("T-UI-01: default theme is black text on a white background", "[theme][T-UI-01]")
{
    const Palette p = defaultPalette();
    CHECK(toHex(p[Role::Background]) == "#FFFFFF");
    CHECK(toHex(p[Role::Text]) == "#000000");
}

TEST_CASE("T-UI-02: every pair in the default theme is at least 4.5:1", "[theme][T-UI-02]")
{
    requireAllPairsAtLeast(defaultPalette(), kMinContrastAA);
}

TEST_CASE("Every role is used in at least one colour pair", "[theme]")
{
    std::set<Role> used;
    for (const auto& pair : colorPairs()) {
        used.insert(pair.foreground);
        used.insert(pair.background);
    }
    CHECK(used.size() == kRoleCount);
}

TEST_CASE("T-UI-03/06/07: enforcing a minimum lifts every pair to it", "[theme][T-UI-03][T-UI-06][T-UI-07]")
{
    const double minimum = GENERATE(4.5, 7.0, 10.0, 15.0, 21.0);
    INFO("minimum " << minimum);
    const auto result = enforceMinimumContrast(defaultPalette(), minimum);
    CHECK(result.unresolved.empty());
    requireAllPairsAtLeast(result.palette, minimum);
}

TEST_CASE("Enforcing AA changes nothing in the default theme", "[theme]")
{
    const auto result = enforceMinimumContrast(defaultPalette(), kMinContrastAA);
    CHECK(result.palette.colors == defaultPalette().colors);
}

TEST_CASE("Max preset gives pure black and white only", "[theme][T-UI-03]")
{
    const auto result = enforceMinimumContrast(defaultPalette(), kMinContrastMax);
    for (std::size_t i = 0; i < kRoleCount; ++i) {
        const std::string h = toHex(result.palette.colors[i]);
        INFO(roleName(static_cast<Role>(i)) << " = " << h);
        CHECK((h == "#000000" || h == "#FFFFFF"));
    }
    CHECK(toHex(result.palette[Role::Background]) == "#FFFFFF");
    CHECK(toHex(result.palette[Role::Text]) == "#000000");
}

TEST_CASE("T-UI-07: a pair at about 5:1 is raised to 7:1 under AAA", "[theme][T-UI-07]")
{
    Palette p = defaultPalette();
    p[Role::TextDisabled] = parseHex("#6B6B6B").value();  // about 5.3:1 on white
    REQUIRE(contrastRatio(p[Role::TextDisabled], p[Role::Background]) < 7.0);
    const auto result = enforceMinimumContrast(p, kMinContrastAAA);
    CHECK(meetsContrast(contrastRatio(result.palette[Role::TextDisabled], result.palette[Role::Background]), 7.0));
}

TEST_CASE("T-UI-05: contrast setting parsing", "[theme][T-UI-05][T-UI-06]")
{
    SECTION("presets, any case")
    {
        CHECK(parseContrastSetting("AA").setting->minimum() == kMinContrastAA);
        CHECK(parseContrastSetting("aaa").setting->minimum() == kMinContrastAAA);
        CHECK(parseContrastSetting(" Max ").setting->minimum() == kMinContrastMax);
    }
    SECTION("custom value 10 is accepted (T-UI-06)")
    {
        auto r = parseContrastSetting("10");
        REQUIRE(r.setting);
        CHECK(r.setting->preset == ContrastPreset::Custom);
        CHECK(r.setting->minimum() == 10.0);
    }
    SECTION("custom value 3 is rejected (T-UI-05)")
    {
        auto r = parseContrastSetting("3.0");
        CHECK_FALSE(r.setting);
        CHECK(r.error.find("4.5") != std::string::npos);
    }
    SECTION("out of range and garbage are rejected")
    {
        CHECK_FALSE(parseContrastSetting("22").setting);
        CHECK_FALSE(parseContrastSetting("high").setting);
        CHECK_FALSE(parseContrastSetting("7x").setting);
        CHECK_FALSE(parseContrastSetting("").setting);
    }
    SECTION("round trip through toString")
    {
        for (const char* s : {"AA", "AAA", "Max", "12.5"}) {
            CHECK(parseContrastSetting(s).setting->toString() == s);
        }
    }
}
