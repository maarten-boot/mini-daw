#include "minidaw/midi_input.hpp"

#include "console_capture.hpp"
#include "minidaw/frame_clock.hpp"
#include "minidaw/smf.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <exception>

namespace minidaw {

MidiInputs::MidiInputs() = default;

MidiInputs::~MidiInputs()
{
    closeAll();
}

MidiInputs::Port* MidiInputs::find(const std::string& name) const
{
    for (const auto& p : ports_) {
        if (p->name == name) return p.get();
    }
    return nullptr;
}

bool MidiInputs::isOpen(const std::string& name) const
{
    return find(name) != nullptr;
}

std::vector<std::string> MidiInputs::setPorts(const std::vector<std::string>& names)
{
    std::vector<std::string> warnings;
    // Close ports that are no longer wanted.
    for (auto it = ports_.begin(); it != ports_.end();) {
        if (std::find(names.begin(), names.end(), (*it)->name) == names.end()) {
            if ((*it)->in) (*it)->in->closePort();
            it = ports_.erase(it);
        } else {
            ++it;
        }
    }
    for (const std::string& name : names) {
        if (name.empty() || find(name) != nullptr) continue;
        auto port = std::make_unique<Port>();
        port->name = name;
        port->capture = &capture_;
        detail::ConsoleCapture capture;
        std::string problem;
        try {
            port->in = std::make_unique<RtMidiIn>();
            port->in->setErrorCallback(
                [](RtMidiError::Type, const std::string& text, void* user) {
                    auto* self = static_cast<MidiInputs*>(user);
                    std::lock_guard<std::mutex> lock(self->messagesMutex_);
                    self->messages_.push_back("MIDI: " + text);
                },
                this);
            unsigned int index = port->in->getPortCount();
            for (unsigned int i = 0; i < port->in->getPortCount(); ++i) {
                if (port->in->getPortName(i) == name) { index = i; break; }
            }
            if (index == port->in->getPortCount()) {
                problem = "MIDI input \"" + name + "\" was not found.";
            } else {
                port->in->setCallback(&MidiInputs::onMessage, port.get());
                port->in->ignoreTypes(true, true, true);  // no SysEx, clock or active sensing (SPEC-REC-04b)
                port->in->openPort(index, "mini-daw");
                if (!port->in->isPortOpen()) problem = "MIDI input \"" + name + "\" could not be opened.";
            }
        } catch (const RtMidiError& e) {
            problem = "MIDI input \"" + name + "\": " + e.getMessage();
        } catch (const std::exception& e) {
            problem = "MIDI input \"" + name + "\": " + e.what();
        }
        for (const std::string& line : capture.lines()) {
            if (!detail::isNoDevicesNotice(line) && problem.find(line) == std::string::npos) {
                warnings.push_back("MIDI: " + line);
            }
        }
        if (!problem.empty()) {
            warnings.push_back(problem);
            continue;
        }
        ports_.push_back(std::move(port));
    }
    return warnings;
}

void MidiInputs::setCapture(bool on)
{
    if (on) {
        RawMidi scratch[64];
        for (auto& p : ports_) {
            while (p->ring.read(scratch, 64) > 0) {
            }
        }
    }
    capture_.store(on, std::memory_order_release);
}

// RtMidi's input thread. No locks, no allocation.
void MidiInputs::onMessage(double /*deltaTime*/, std::vector<unsigned char>* message, void* user)
{
    const std::int64_t now = steadyNowNs();
    auto* port = static_cast<Port*>(user);
    if (message == nullptr || message->empty()) return;
    port->seen.fetch_add(1, std::memory_order_relaxed);
    if (!port->capture->load(std::memory_order_acquire)) return;
    const std::uint8_t len = channelMessageSize((*message)[0]);
    if (len == 0 || message->size() < len) return;
    RawMidi m;
    m.ns = now;
    m.size = len;
    for (std::uint8_t i = 0; i < len; ++i) m.data[i] = (*message)[i];
    if (!port->ring.write(&m, 1)) port->lost.fetch_add(1, std::memory_order_relaxed);
}

std::size_t MidiInputs::read(const std::string& name, RawMidi* out, std::size_t max)
{
    Port* p = find(name);
    return p ? p->ring.read(out, max) : 0;
}

std::uint64_t MidiInputs::messagesSeen(const std::string& name) const
{
    const Port* p = find(name);
    return p ? p->seen.load(std::memory_order_relaxed) : 0;
}

std::uint64_t MidiInputs::messagesLost(const std::string& name) const
{
    const Port* p = find(name);
    return p ? p->lost.load(std::memory_order_relaxed) : 0;
}

std::vector<std::string> MidiInputs::takeMessages()
{
    std::lock_guard<std::mutex> lock(messagesMutex_);
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}

} // namespace minidaw
