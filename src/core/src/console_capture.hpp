#pragma once

// Internal helper: RtAudio and RtMidi print some messages straight to
// std::cerr (for example while their constructors search for devices, before
// an error callback can be attached). ConsoleCapture collects those lines so
// they can be shown as warnings instead of appearing on the console.
//
// std::cerr is process-wide. This is safe while only the GUI thread writes to
// it; revisit if other threads start logging to std::cerr.

#include <algorithm>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

namespace minidaw::detail {

// ALSA (Linux) prints its own errors straight to stderr, e.g. "cannot find
// card '0'" on a machine without a sound card. Those bypass std::cerr, so
// install a silent ALSA error handler instead. The failures still reach us
// through RtAudio/RtMidi return values. Does nothing on other systems; safe
// to call more than once.
void silenceAlsaMessages();

class ConsoleCapture {
public:
    ConsoleCapture() : old_(std::cerr.rdbuf(buffer_.rdbuf())) { silenceAlsaMessages(); }
    ~ConsoleCapture() { std::cerr.rdbuf(old_); }
    ConsoleCapture(const ConsoleCapture&) = delete;
    ConsoleCapture& operator=(const ConsoleCapture&) = delete;

    // Non-empty lines captured so far.
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

// "No devices" is a normal situation, already visible as an empty list.
inline bool isNoDevicesNotice(const std::string& text)
{
    return text.find("no MIDI input devices currently available") != std::string::npos ||
           text.find("no MIDI output devices currently available") != std::string::npos ||
           text.find("No devices found") != std::string::npos;
}

// Append captured lines to `out` (with a prefix), skipping "no devices"
// notices and lines already present.
inline void appendCaptured(const ConsoleCapture& capture, const std::string& prefix, std::vector<std::string>& out)
{
    for (const std::string& line : capture.lines()) {
        if (isNoDevicesNotice(line)) continue;
        const std::string text = prefix + line;
        if (std::find(out.begin(), out.end(), text) == out.end()) out.push_back(text);
    }
}

} // namespace minidaw::detail
