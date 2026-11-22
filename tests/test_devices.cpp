// Smoke tests for device enumeration (SPEC-DEV-01, partial).
// These must pass on machines without any audio or MIDI hardware (CI runners),
// so they only check that scanning works and returns consistent data.

#include "minidaw/devices.hpp"
#include "minidaw/mic_access.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

TEST_CASE("scanDevices does not throw and returns consistent data", "[devices]")
{
    minidaw::DeviceScan scan;
    REQUIRE_NOTHROW(scan = minidaw::scanDevices());

    std::set<unsigned int> ids;
    for (const auto& d : scan.audioDevices) {
        CHECK_FALSE(d.name.empty());
        CHECK((d.inputChannels > 0 || d.outputChannels > 0));
        CHECK(ids.insert(d.id).second);  // ids are unique
    }

    for (unsigned int i = 0; i < scan.midiInputs.size(); ++i) {
        CHECK(scan.midiInputs[i].index == i);
    }
    for (unsigned int i = 0; i < scan.midiOutputs.size(); ++i) {
        CHECK(scan.midiOutputs[i].index == i);
    }
}

TEST_CASE("Scanning twice gives the same device list", "[devices]")
{
    const auto a = minidaw::scanDevices();
    const auto b = minidaw::scanDevices();
    REQUIRE(a.audioDevices.size() == b.audioDevices.size());
    REQUIRE(a.midiInputs.size() == b.midiInputs.size());
    REQUIRE(a.midiOutputs.size() == b.midiOutputs.size());
}

TEST_CASE("T-MAC-06: the microphone permission: a plain line when mini-daw may not record", "[mac][T-MAC-06]")
{
    using minidaw::MicAccess;
    CHECK(minidaw::micAccessWarning(MicAccess::NotNeeded).empty());
    CHECK(minidaw::micAccessWarning(MicAccess::Allowed).empty());
    for (const MicAccess a : {MicAccess::NotAsked, MicAccess::Refused, MicAccess::Restricted}) {
        const std::string line = minidaw::micAccessWarning(a);
        CHECK(line.find("microphone") != std::string::npos);
        CHECK(line.find("silence") != std::string::npos);
    }
    CHECK(minidaw::micAccessWarning(MicAccess::Refused).find("System Settings > Privacy & Security > Microphone") !=
          std::string::npos);
#if defined(__APPLE__)
    // Only read here: asking would show macOS's question on a developer's Mac.
    CHECK(minidaw::micAccess() != MicAccess::NotNeeded);
#else
    CHECK(minidaw::micAccess() == MicAccess::NotNeeded);
    CHECK_FALSE(minidaw::askMicAccess());
    CHECK(minidaw::micAccessAnswers() == 0);
#endif
}
