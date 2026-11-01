#include "minidaw/devices.hpp"

#include <RtAudio.h>
#include <RtMidi.h>

#include <exception>
#include <memory>

namespace minidaw {
namespace {

template <typename RtMidiT>
std::vector<MidiPortInfo> listMidiPorts(const char* direction, std::vector<std::string>& warnings)
{
    std::vector<MidiPortInfo> ports;
    try {
        RtMidiT midi;
        const unsigned int count = midi.getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            ports.push_back({i, midi.getPortName(i)});
        }
    } catch (const RtMidiError& e) {
        warnings.push_back(std::string("MIDI ") + direction + ": " + e.getMessage());
    } catch (const std::exception& e) {
        warnings.push_back(std::string("MIDI ") + direction + ": " + e.what());
    }
    return ports;
}

} // namespace

DeviceScan scanDevices()
{
    DeviceScan scan;

    // RtAudio 6 reports errors through a callback instead of exceptions.
    auto onError = [&scan](RtAudioErrorType type, const std::string& text) {
        if (type != RTAUDIO_NO_ERROR) {
            scan.warnings.push_back("Audio: " + text);
        }
    };

    try {
        RtAudio audio(RtAudio::UNSPECIFIED, onError);
        scan.audioApi = RtAudio::getApiDisplayName(audio.getCurrentApi());
        for (unsigned int id : audio.getDeviceIds()) {
            const RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
            AudioDeviceInfo dev;
            dev.id = id;
            dev.name = info.name;
            dev.inputChannels = info.inputChannels;
            dev.outputChannels = info.outputChannels;
            dev.isDefaultInput = info.isDefaultInput;
            dev.isDefaultOutput = info.isDefaultOutput;
            dev.sampleRates = info.sampleRates;
            dev.preferredSampleRate = info.preferredSampleRate;
            scan.audioDevices.push_back(std::move(dev));
        }
    } catch (const std::exception& e) {
        scan.warnings.push_back(std::string("Audio: ") + e.what());
    }

    scan.midiInputs = listMidiPorts<RtMidiIn>("input", scan.warnings);
    scan.midiOutputs = listMidiPorts<RtMidiOut>("output", scan.warnings);
    return scan;
}

} // namespace minidaw
