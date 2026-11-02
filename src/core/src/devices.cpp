#include "minidaw/devices.hpp"
#include "minidaw/midi_input.hpp"

#include "console_capture.hpp"

#include <RtAudio.h>
#include <RtMidi.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>

namespace minidaw {
namespace {

template <typename RtMidiT>
std::vector<MidiPortInfo> listMidiPorts(const char* direction, std::vector<std::string>& warnings)
{
    std::vector<MidiPortInfo> ports;
    std::vector<std::string> found;
    detail::ConsoleCapture capture;
    try {
        RtMidiT midi;
        const unsigned int count = midi.getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            std::string name = midi.getPortName(i);
            if (!isOwnMidiPort(name)) ports.push_back({i, std::move(name)});
        }
    } catch (const RtMidiError& e) {
        found.push_back(e.getMessage());
    } catch (const std::exception& e) {
        found.push_back(e.what());
    }
    // RtMidi also prints the text of an error it throws; avoid listing it twice.
    for (const std::string& line : capture.lines()) {
        if (std::find(found.begin(), found.end(), line) == found.end()) found.push_back(line);
    }
    for (const std::string& text : found) {
        if (!detail::isNoDevicesNotice(text)) warnings.push_back(std::string("MIDI ") + direction + ": " + text);
    }
    return ports;
}

} // namespace

DeviceScan scanDevices()
{
    DeviceScan scan;

    // RtAudio 6 reports errors through a callback instead of exceptions.
    auto onError = [&scan](RtAudioErrorType type, const std::string& text) {
        if (type != RTAUDIO_NO_ERROR && !detail::isNoDevicesNotice(text)) {
            scan.warnings.push_back("Audio: " + text);
        }
    };

    detail::ConsoleCapture capture;  // RtAudio's constructor may print before onError is attached
    try {
        RtAudio audio(RtAudio::UNSPECIFIED, onError);
        audio.showWarnings(false);
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
    detail::appendCaptured(capture, "Audio: ", scan.warnings);

    scan.midiInputs = listMidiPorts<RtMidiIn>("input", scan.warnings);
    scan.midiOutputs = listMidiPorts<RtMidiOut>("output", scan.warnings);
    return scan;
}

} // namespace minidaw
