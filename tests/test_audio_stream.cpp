// Device selection logic (SPEC-DEV-02/03) and level formatting.
// Opening a real stream is covered by `mini-daw --audio-test` (manual T-DEV-02).

#include "minidaw/audio_stream.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace minidaw;

namespace {

std::vector<AudioDeviceInfo> machine()
{
    // Like the user's Windows 10 machine, plus a USB interface.
    std::vector<AudioDeviceInfo> d(3);
    d[0].id = 129; d[0].name = "Speakers (High Definition Audio Device)"; d[0].outputChannels = 2; d[0].isDefaultOutput = true;
    d[1].id = 130; d[1].name = "Line In (High Definition Audio Device)"; d[1].inputChannels = 2; d[1].isDefaultInput = true;
    d[2].id = 131; d[2].name = "USB Interface"; d[2].inputChannels = 64; d[2].outputChannels = 8;
    return d;
}

} // namespace

TEST_CASE("Default devices are used for (default)", "[audio][T-DEV-02]")
{
    auto in = resolveAudioDevice(machine(), kDefaultDevice, Direction::Input);
    auto out = resolveAudioDevice(machine(), kDefaultDevice, Direction::Output);
    CHECK(in.id == 130u);
    CHECK(in.channels == 2);
    CHECK(out.id == 129u);
    CHECK(in.warning.empty());
}

TEST_CASE("A device is found by name, channel counts are capped", "[audio][T-DEV-02]")
{
    auto in = resolveAudioDevice(machine(), "USB Interface", Direction::Input);
    auto out = resolveAudioDevice(machine(), "USB Interface", Direction::Output);
    CHECK(in.id == 131u);
    CHECK(in.channels == kMaxInputChannels);
    CHECK(out.channels == kMaxOutputChannels);
}

TEST_CASE("(none) opens nothing in that direction", "[audio]")
{
    auto in = resolveAudioDevice(machine(), kNoDevice, Direction::Input);
    CHECK_FALSE(in.id.has_value());
    CHECK(in.warning.empty());
}

TEST_CASE("T-DEV-04: a missing saved device falls back to the default with a warning", "[audio][T-DEV-04]")
{
    auto in = resolveAudioDevice(machine(), "Unplugged Mic", Direction::Input);
    CHECK(in.id == 130u);
    CHECK(in.warning.find("Unplugged Mic") != std::string::npos);
    CHECK(in.warning.find("Line In") != std::string::npos);
}

TEST_CASE("An output-only device is not used as input", "[audio]")
{
    auto in = resolveAudioDevice(machine(), "Speakers (High Definition Audio Device)", Direction::Input);
    CHECK(in.id == 130u);  // fell back
    CHECK_FALSE(in.warning.empty());
}

TEST_CASE("No devices at all gives a warning and no id", "[audio][T-DEV-04]")
{
    auto in = resolveAudioDevice({}, kDefaultDevice, Direction::Input);
    CHECK_FALSE(in.id.has_value());
    CHECK_FALSE(in.warning.empty());
}

TEST_CASE("Without a marked default, the first usable device is used", "[audio]")
{
    auto devs = machine();
    for (auto& d : devs) d.isDefaultInput = false;
    auto in = resolveAudioDevice(devs, kDefaultDevice, Direction::Input);
    CHECK(in.id == 130u);
}

TEST_CASE("Peak levels are shown in dBFS", "[audio][T-UI-09]")
{
    CHECK(formatDbfs(1.0f) == "0.0 dBFS");
    CHECK(formatDbfs(0.5f) == "-6.0 dBFS");
    CHECK(formatDbfs(0.0f) == "-inf dBFS");
}
