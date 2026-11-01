// Smoke tests for device enumeration (SPEC-DEV-01, partial).
// These must pass on machines without any audio or MIDI hardware (CI runners),
// so they only check that scanning works and returns consistent data.

#include "minidaw/devices.hpp"

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
