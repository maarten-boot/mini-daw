#pragma once

// Device enumeration (first part of SPEC-DEV-01).
// Device selection and persistence (SPEC-DEV-02/03) follow in step P4.

#include <string>
#include <vector>

namespace minidaw {

struct AudioDeviceInfo {
    unsigned int id = 0;               // RtAudio device id (stable while the device is plugged in)
    std::string name;
    unsigned int inputChannels = 0;
    unsigned int outputChannels = 0;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
    std::vector<unsigned int> sampleRates;
    unsigned int preferredSampleRate = 0;
};

struct MidiPortInfo {
    unsigned int index = 0;            // RtMidi port number
    std::string name;
};

struct DeviceScan {
    std::string audioApi;              // e.g. "WASAPI", "ALSA"
    std::vector<AudioDeviceInfo> audioDevices;
    std::vector<MidiPortInfo> midiInputs;
    std::vector<MidiPortInfo> midiOutputs;
    std::vector<std::string> warnings; // non-fatal problems found while scanning
};

// Scan all audio devices and MIDI ports. Never throws; problems are reported
// in DeviceScan::warnings.
DeviceScan scanDevices();

} // namespace minidaw
