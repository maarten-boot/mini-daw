#include "minidaw/midi_input.hpp"

#include "console_capture.hpp"
#include "minidaw/frame_clock.hpp"
#include "minidaw/smf.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <exception>
#include <optional>

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

std::string midiPortBaseName(std::string_view name)
{
    // Trailing " <digits>" or " <digits>:<digits>".
    std::size_t end = name.size();
    std::size_t i = end;
    auto digitsBefore = [&](std::size_t pos) {
        std::size_t j = pos;
        while (j > 0 && name[j - 1] >= '0' && name[j - 1] <= '9') --j;
        return j;
    };
    std::size_t j = digitsBefore(i);
    if (j == i) return std::string(name);
    if (j > 0 && name[j - 1] == ':') {
        const std::size_t k = digitsBefore(j - 1);
        if (k == j - 1) return std::string(name);
        j = k;
    }
    if (j == 0 || name[j - 1] != ' ') return std::string(name);
    return std::string(name.substr(0, j - 1));
}

std::optional<std::size_t> matchMidiPort(const std::vector<std::string>& available, const std::string& wanted)
{
    for (std::size_t i = 0; i < available.size(); ++i) {
        if (available[i] == wanted) return i;
    }
    const std::string base = midiPortBaseName(wanted);
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < available.size(); ++i) {
        if (midiPortBaseName(available[i]) != base) continue;
        if (found) return std::nullopt;  // several devices of the same kind: do not guess
        found = i;
    }
    return found;
}

bool MidiInputs::isOpen(const std::string& name) const
{
    const Port* p = find(name);
    return p != nullptr && p->connected.load(std::memory_order_acquire);
}

std::uint64_t MidiInputs::disconnects(const std::string& name) const
{
    const Port* p = find(name);
    return p ? p->disconnects.load(std::memory_order_relaxed) : 0;
}

RtMidiIn* MidiInputs::scanner(std::string& error)
{
    if (scanner_) return scanner_.get();
    detail::ConsoleCapture capture;
    try {
        scanner_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "mini-daw scan");
        scanner_->setErrorCallback([](RtMidiError::Type, const std::string&, void*) {}, nullptr);
    } catch (const RtMidiError& e) {
        error = "MIDI: " + e.getMessage();
        scanner_.reset();
    }
    return scanner_.get();
}

std::vector<std::string> MidiInputs::availablePorts()
{
    std::string error;
    std::vector<std::string> names;
    RtMidiIn* s = scanner(error);
    if (!s) return names;
    detail::ConsoleCapture capture;  // RtMidi prints warnings for ports that vanish while listing
    try {
        const unsigned int n = s->getPortCount();
        for (unsigned int i = 0; i < n; ++i) {
            std::string name = s->getPortName(i);
            if (!name.empty()) names.push_back(std::move(name));
        }
    } catch (const RtMidiError&) {
    }
    return names;
}

std::string MidiInputs::connect(Port& port, const std::string& actualName)
{
    detail::ConsoleCapture capture;
    std::string problem;
    try {
        auto in = std::make_unique<RtMidiIn>();
        in->setErrorCallback(
            [](RtMidiError::Type, const std::string& text, void* user) {
                auto* self = static_cast<MidiInputs*>(user);
                std::lock_guard<std::mutex> lock(self->messagesMutex_);
                self->messages_.push_back("MIDI: " + text);
            },
            this);
        unsigned int index = in->getPortCount();
        for (unsigned int i = 0; i < in->getPortCount(); ++i) {
            if (in->getPortName(i) == actualName) { index = i; break; }
        }
        if (index == in->getPortCount()) {
            problem = "MIDI input \"" + port.name + "\" was not found.";
        } else {
            in->setCallback(&MidiInputs::onMessage, &port);
            in->ignoreTypes(true, true, true);  // no SysEx, clock or active sensing (SPEC-REC-04b)
            in->openPort(index, "mini-daw");
            if (!in->isPortOpen()) problem = "MIDI input \"" + port.name + "\" could not be opened.";
        }
        if (problem.empty()) {
            port.in = std::move(in);
            port.openedAs = actualName;
            port.connected.store(true, std::memory_order_release);
        }
    } catch (const RtMidiError& e) {
        problem = "MIDI input \"" + port.name + "\": " + e.getMessage();
    } catch (const std::exception& e) {
        problem = "MIDI input \"" + port.name + "\": " + e.what();
    }
    for (const std::string& line : capture.lines()) {
        if (!detail::isNoDevicesNotice(line) && problem.find(line) == std::string::npos) {
            std::lock_guard<std::mutex> lock(messagesMutex_);
            messages_.push_back("MIDI: " + line);
        }
    }
    return problem;
}

void MidiInputs::disconnect(Port& port)
{
    port.connected.store(false, std::memory_order_release);
    if (port.in) {
        detail::ConsoleCapture capture;  // closing a device that has gone may print a warning
        try {
            port.in->closePort();
        } catch (const RtMidiError&) {
        }
        port.in.reset();  // its input thread has stopped: the ring has no writer now
    }
}

std::vector<MidiPortEvent> MidiInputs::setPorts(const std::vector<std::string>& names)
{
    // Close and forget ports that are no longer wanted.
    for (auto it = ports_.begin(); it != ports_.end();) {
        if (std::find(names.begin(), names.end(), (*it)->name) == names.end()) {
            disconnect(**it);
            it = ports_.erase(it);
        } else {
            ++it;
        }
    }
    bool added = false;
    for (const std::string& name : names) {
        if (name.empty() || find(name) != nullptr) continue;
        auto port = std::make_unique<Port>();
        port->name = name;
        port->capture = &capture_;
        ports_.push_back(std::move(port));
        added = true;
    }
    return added ? checkConnections() : std::vector<MidiPortEvent>{};
}

std::vector<MidiPortEvent> MidiInputs::checkConnections()
{
    std::vector<MidiPortEvent> events;
    if (ports_.empty()) return events;
    const std::vector<std::string> available = availablePorts();
    for (auto& p : ports_) {
        Port& port = *p;
        const bool connected = port.connected.load(std::memory_order_relaxed);
        if (connected) {
            // Still there, perhaps under a new number (Windows renumbers the
            // other devices when one is unplugged): the open handle stays valid.
            const auto still = matchMidiPort(available, port.openedAs);
            if (still) {
                port.openedAs = available[*still];
            } else {
                disconnect(port);
                port.disconnects.fetch_add(1, std::memory_order_relaxed);
                events.push_back({MidiPortEvent::Kind::Disconnected, port.name, port.openedAs});
                // The next checks find it missing: that is already reported.
                port.lastProblem = "MIDI input \"" + port.name + "\" was not found.";
            }
            continue;
        }
        const auto index = matchMidiPort(available, port.name);
        std::string problem;
        if (!index) {
            problem = "MIDI input \"" + port.name + "\" was not found.";
        } else {
            problem = connect(port, available[*index]);
        }
        if (problem.empty()) {
            events.push_back({port.everConnected ? MidiPortEvent::Kind::Reconnected : MidiPortEvent::Kind::Opened,
                              port.name, port.openedAs});
            port.everConnected = true;
            port.lastProblem.clear();
        } else if (problem != port.lastProblem) {
            events.push_back({MidiPortEvent::Kind::Failed, port.name, problem});
            port.lastProblem = problem;
        }
    }
    return events;
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
