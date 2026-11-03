#include "minidaw/midi_input.hpp"

#include "console_capture.hpp"
#include "minidaw/frame_clock.hpp"
#include "minidaw/smf.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <thread>

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

MidiNaming nativeMidiNaming()
{
#if defined(_WIN32)
    return MidiNaming::WinMM;
#elif defined(__linux__)
    return MidiNaming::Alsa;
#else
    return MidiNaming::Plain;
#endif
}

namespace {

// Start of the digits that end at `end` (== end if none).
std::size_t digitsStart(std::string_view s, std::size_t end)
{
    std::size_t j = end;
    while (j > 0 && s[j - 1] >= '0' && s[j - 1] <= '9') --j;
    return j;
}

} // namespace

std::string midiPortBaseName(std::string_view name, MidiNaming naming)
{
    if (naming == MidiNaming::Plain) return std::string(name);
    std::size_t j = digitsStart(name, name.size());
    if (j == name.size()) return std::string(name);  // no number at the end
    if (naming == MidiNaming::Alsa) {
        // " <client>:<port>"
        if (j == 0 || name[j - 1] != ':') return std::string(name);
        const std::size_t k = digitsStart(name, j - 1);
        if (k == j - 1) return std::string(name);
        j = k;
    }
    if (j == 0 || name[j - 1] != ' ') return std::string(name);
    return std::string(name.substr(0, j - 1));
}

bool isOwnMidiPort(std::string_view name)
{
    return name.rfind("RtMidi Input Client:mini-daw", 0) == 0 || name.rfind("RtMidi Output Client:mini-daw", 0) == 0;
}

std::optional<std::size_t> matchMidiPort(const std::vector<std::string>& available, const std::string& wanted,
                                         MidiNaming naming)
{
    for (std::size_t i = 0; i < available.size(); ++i) {
        if (available[i] == wanted) return i;
    }
    const std::string base = midiPortBaseName(wanted, naming);
    if (base == wanted) return std::nullopt;  // no backend number: only the exact name counts
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < available.size(); ++i) {
        if (midiPortBaseName(available[i], naming) != base) continue;
        if (found) return std::nullopt;  // several devices of the same kind: do not guess
        found = i;
    }
    return found;
}

MidiCheckPlan planMidiCheck(const std::vector<MidiPortState>& ports, const std::vector<std::string>& available,
                            MidiNaming naming)
{
    MidiCheckPlan plan;
    std::vector<bool> claimed(available.size(), false);  // an available port serves one track port at most

    // 1. Connected ports, per base name, by count.
    std::vector<bool> done(ports.size(), false);
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (!ports[i].connected || done[i]) continue;
        const std::string base = midiPortBaseName(ports[i].openedAs, naming);
        std::vector<std::size_t> group;
        for (std::size_t j = i; j < ports.size(); ++j) {
            if (ports[j].connected && !done[j] && midiPortBaseName(ports[j].openedAs, naming) == base) {
                group.push_back(j);
                done[j] = true;
            }
        }
        std::vector<std::size_t> present;
        for (std::size_t a = 0; a < available.size(); ++a) {
            if (midiPortBaseName(available[a], naming) == base) present.push_back(a);
        }
        if (present.size() >= group.size()) {
            // All still there. Claim the entries: the exact name first, else a
            // free one (the system renumbered it; the handle stays valid).
            for (std::size_t g : group) {
                for (std::size_t a : present) {
                    if (!claimed[a] && available[a] == ports[g].openedAs) { claimed[a] = true; break; }
                }
            }
            for (std::size_t g : group) {
                const bool exact = std::any_of(present.begin(), present.end(), [&](std::size_t a) {
                    return available[a] == ports[g].openedAs;
                });
                if (exact) continue;
                for (std::size_t a : present) {
                    if (claimed[a]) continue;
                    claimed[a] = true;
                    plan.rename.push_back({g, available[a]});
                    break;
                }
            }
        } else if (group.size() == 1 || present.empty()) {
            for (std::size_t g : group) plan.disconnect.push_back(g);  // gone; reopened when back
        } else {
            for (std::size_t g : group) plan.block.push_back(g);       // cannot tell which one went
            for (std::size_t a : present) claimed[a] = true;           // do not reopen them as others
        }
    }

    // 2. Ports that are not connected (and not blocked): open when there.
    std::vector<std::string> free;
    std::vector<std::size_t> freeIndex;
    for (std::size_t a = 0; a < available.size(); ++a) {
        if (!claimed[a]) { free.push_back(available[a]); freeIndex.push_back(a); }
    }
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (ports[i].connected || ports[i].blocked) continue;
        const auto m = matchMidiPort(free, ports[i].name, naming);
        if (!m) continue;
        plan.connect.push_back({i, free[*m]});
        free.erase(free.begin() + static_cast<std::ptrdiff_t>(*m));
        freeIndex.erase(freeIndex.begin() + static_cast<std::ptrdiff_t>(*m));
    }
    return plan;
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
            if (!name.empty() && !isOwnMidiPort(name)) names.push_back(std::move(name));
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
        applyTaps(*port);
        ports_.push_back(std::move(port));
        added = true;
    }
    return added ? checkConnections() : std::vector<MidiPortEvent>{};
}

bool MidiInputs::isBlocked(const std::string& name) const
{
    const Port* p = find(name);
    return p != nullptr && p->blocked;
}

std::vector<MidiPortEvent> MidiInputs::checkConnections()
{
    std::vector<MidiPortEvent> events;
    if (ports_.empty()) return events;
    const std::vector<std::string> available = availablePorts();
    std::vector<MidiPortState> states;
    for (const auto& p : ports_) {
        states.push_back({p->name, p->connected.load(std::memory_order_relaxed), p->openedAs, p->blocked});
    }
    const MidiCheckPlan plan = planMidiCheck(states, available);

    for (const auto& [i, name] : plan.rename) ports_[i]->openedAs = name;
    for (std::size_t i : plan.disconnect) {
        Port& port = *ports_[i];
        disconnect(port);
        port.disconnects.fetch_add(1, std::memory_order_relaxed);
        events.push_back({MidiPortEvent::Kind::Disconnected, port.name, port.openedAs});
        // The next checks find it missing: that is already reported.
        port.lastProblem = "MIDI input \"" + port.name + "\" was not found.";
    }
    for (std::size_t i : plan.block) {
        Port& port = *ports_[i];
        disconnect(port);
        port.disconnects.fetch_add(1, std::memory_order_relaxed);
        port.blocked = true;
        events.push_back({MidiPortEvent::Kind::Ambiguous, port.name, port.openedAs});
    }
    std::vector<bool> connecting(ports_.size(), false);
    for (const auto& [i, actual] : plan.connect) {
        connecting[i] = true;
        Port& port = *ports_[i];
        const std::string problem = connect(port, actual);
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
    // Wanted, not connected and not found: report once.
    for (std::size_t i = 0; i < ports_.size(); ++i) {
        Port& port = *ports_[i];
        if (connecting[i] || port.blocked || port.connected.load(std::memory_order_relaxed)) continue;
        const std::string problem = "MIDI input \"" + port.name + "\" was not found.";
        if (problem != port.lastProblem) {
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
    const std::uint8_t len = channelMessageSize((*message)[0]);
    if (len == 0 || message->size() < len) return;
    RawMidi m;
    m.ns = now;
    m.size = len;
    for (std::uint8_t i = 0; i < len; ++i) m.data[i] = (*message)[i];
    // Live taps for instruments (also while not recording).
    port->inCallback.fetch_add(1, std::memory_order_seq_cst);
    for (auto& t : port->taps) {
        if (SpscRing<RawMidi>* ring = t.load(std::memory_order_seq_cst)) ring->write(&m, 1);
    }
    port->inCallback.fetch_sub(1, std::memory_order_seq_cst);
    if (!port->capture->load(std::memory_order_acquire)) return;
    if (!port->ring.write(&m, 1)) port->lost.fetch_add(1, std::memory_order_relaxed);
}

void MidiInputs::applyTaps(Port& port)
{
    const auto it = tapsByPort_.find(port.name);
    for (std::size_t i = 0; i < port.taps.size(); ++i) {
        SpscRing<RawMidi>* ring = (it != tapsByPort_.end() && i < it->second.size()) ? it->second[i] : nullptr;
        port.taps[i].store(ring, std::memory_order_seq_cst);
    }
}

bool MidiInputs::addTap(const std::string& name, SpscRing<RawMidi>* ring)
{
    auto& list = tapsByPort_[name];
    if (std::find(list.begin(), list.end(), ring) != list.end()) return true;
    if (list.size() >= 8) return false;
    list.push_back(ring);
    if (Port* p = find(name)) applyTaps(*p);
    return true;
}

void MidiInputs::removeTap(const std::string& name, SpscRing<RawMidi>* ring)
{
    auto it = tapsByPort_.find(name);
    if (it == tapsByPort_.end()) return;
    it->second.erase(std::remove(it->second.begin(), it->second.end(), ring), it->second.end());
    if (Port* p = find(name)) {
        applyTaps(*p);
        // A callback that read the old pointer may still be writing: wait for it.
        while (p->inCallback.load(std::memory_order_seq_cst) != 0) std::this_thread::yield();
    }
    if (it->second.empty()) tapsByPort_.erase(it);
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
