#include "minidaw/devices.hpp"

#include <RtAudio.h>
#include <RtMidi.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>

namespace minidaw {
namespace {

// RtMidi prints warnings to std::cerr while its constructor runs, before an
// error callback can be installed. Capture them so they end up in
// DeviceScan::warnings instead of on the console.
// Note: std::cerr is process-wide. This is safe while only the GUI thread
// writes to it; revisit if other threads start logging to std::cerr.
class CerrCapture {
public:
    CerrCapture() : old_(std::cerr.rdbuf(buffer_.rdbuf())) {}
    ~CerrCapture() { std::cerr.rdbuf(old_); }
    CerrCapture(const CerrCapture&) = delete;
    CerrCapture& operator=(const CerrCapture&) = delete;

    std::vector<std::string> lines() const
    {
        std::vector<std::string> out;
        std::istringstream in(buffer_.str());
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) out.push_back(line);
        }
        return out;
    }

private:
    std::ostringstream buffer_;
    std::streambuf* old_;
};

// "No devices" is a normal situation, already shown as an empty list.
bool isNoDevicesNotice(const std::string& text)
{
    return text.find("no MIDI input devices currently available") != std::string::npos ||
           text.find("no MIDI output devices currently available") != std::string::npos;
}

template <typename RtMidiT>
std::vector<MidiPortInfo> listMidiPorts(const char* direction, std::vector<std::string>& warnings)
{
    std::vector<MidiPortInfo> ports;
    std::vector<std::string> found;
    CerrCapture capture;
    try {
        RtMidiT midi;
        const unsigned int count = midi.getPortCount();
        for (unsigned int i = 0; i < count; ++i) {
            ports.push_back({i, midi.getPortName(i)});
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
        if (!isNoDevicesNotice(text)) warnings.push_back(std::string("MIDI ") + direction + ": " + text);
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
