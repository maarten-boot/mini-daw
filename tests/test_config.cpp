#include "minidaw/config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("minidaw-test-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("T-UI-04: min_contrast = 7.0 in the config file", "[config][T-UI-04]")
{
    auto r = parseConfig("[ui]\nmin_contrast = 7.0\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.minimum() == 7.0);
}

TEST_CASE("Config: presets, comments, case and CRLF", "[config]")
{
    auto r = parseConfig("; comment\r\n# other comment\r\n[UI]\r\nMin_Contrast = max ; inline comment\r\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.preset == ContrastPreset::Max);
}

TEST_CASE("T-UI-05: config value below 4.5 is rejected and AA is kept", "[config][T-UI-05]")
{
    auto r = parseConfig("[ui]\nmin_contrast = 3\n", "test.ini");
    REQUIRE(r.warnings.size() == 1);
    CHECK(r.warnings[0].find("test.ini line 2") != std::string::npos);
    CHECK(r.config.contrast.minimum() == kMinContrastAA);
}

TEST_CASE("Config: unknown keys and bad lines give warnings, not errors", "[config]")
{
    auto r = parseConfig("[ui]\ncolour = red\nnot a setting\n[\n");
    CHECK(r.warnings.size() == 3);
    CHECK(r.config.contrast.minimum() == kMinContrastAA);
}

TEST_CASE("Empty or missing config gives defaults", "[config][T-UI-01]")
{
    CHECK(parseConfig("").config.contrast.minimum() == kMinContrastAA);
    auto r = loadConfigFile("/this/path/does/not/exist/config.ini");
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.minimum() == kMinContrastAA);
}

TEST_CASE("The default config text parses without warnings", "[config]")
{
    auto r = parseConfig(defaultConfigText());
    CHECK(r.warnings.empty());
    CHECK(r.config.contrast.preset == ContrastPreset::AA);
}

TEST_CASE("setConfigValue keeps comments and other lines", "[config]")
{
    const std::string before = "; my notes\n[ui]\n; keep me\nmin_contrast = AA\n\n[other]\nx = 1\n";
    const std::string after = setConfigValue(before, "ui", "min_contrast", "AAA");
    CHECK(after == "; my notes\n[ui]\n; keep me\nmin_contrast = AAA\n\n[other]\nx = 1\n");
}

TEST_CASE("setConfigValue adds a missing key or section", "[config]")
{
    CHECK(setConfigValue("[ui]\n", "ui", "min_contrast", "Max") == "[ui]\nmin_contrast = Max\n");
    CHECK(setConfigValue("[ui]\n\n[other]\nx = 1\n", "ui", "min_contrast", "Max") ==
          "[ui]\nmin_contrast = Max\n\n[other]\nx = 1\n");
    CHECK(setConfigValue("; header\n", "ui", "min_contrast", "Max") == "; header\n\n[ui]\nmin_contrast = Max\n");
    CHECK(setConfigValue("", "ui", "min_contrast", "AA") == "[ui]\nmin_contrast = AA\n");
}

TEST_CASE("T-UI-03/04: an interactive change is saved and read back", "[config][T-UI-03][T-UI-04]")
{
    TempDir dir;
    const fs::path file = dir.path / "sub" / "config.ini";

    SECTION("new file is created with the default comments")
    {
        REQUIRE(saveContrastSetting(file, {ContrastPreset::AAA}).empty());
        const std::string text = readFile(file);
        CHECK(text.find("; mini-daw configuration") != std::string::npos);
        CHECK(loadConfigFile(file).config.contrast.preset == ContrastPreset::AAA);
    }
    SECTION("existing file keeps the user's own comments")
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file) << "; my own note\n[ui]\nmin_contrast = AA\n";
        REQUIRE(saveContrastSetting(file, {ContrastPreset::Custom, 12.5}).empty());
        const std::string text = readFile(file);
        CHECK(text.find("; my own note") != std::string::npos);
        CHECK(loadConfigFile(file).config.contrast.minimum() == 12.5);
    }
}
