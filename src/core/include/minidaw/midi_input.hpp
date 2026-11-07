#pragma once

// MIDI input ports for recording (SPEC-REC-04, 04a).
//
// Each open port has an RtMidiIn whose callback (on RtMidi's own thread)
// stamps every message with the steady clock and puts it into a lock-free
// ring for that port. The recording session empties the rings and places the
// messages on the audio timeline. While capture is off, messages are only
// counted (activity shown in the GUI) and not queued.
//
// Windows (WinMM) lets only one program open a MIDI input port, so ports are
// opened only for armed MIDI tracks.
//
// Hot-plug (SPEC-REC-01b): checkConnections() re-reads the port list. A port
// that has gone is closed but kept (with its ring), so a recording session
// can go on reading it; when the device returns it is opened again. The list
// of kept ports only changes in setPorts(), which is not used while
// recording, so the session thread never sees it change.

#include "minidaw/ring_buffer.hpp"
#include "minidaw/session_io.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// RtMidi 6.1 declares its classes in rt::midi (and adds `using namespace rt::midi`).
namespace rt::midi {
class RtMidiIn;
}

namespace minidaw {

struct RawMidi {
    std::int64_t ns = 0;                     // steadyNowNs() when it arrived
    std::array<std::uint8_t, 3> data{};
    std::uint8_t size = 0;
};

// RtMidi adds a number to port names: Windows (WinMM) " 0" (the device
// index), ALSA " 24:0" (client:port). Both can change when a device is
// plugged in again or another one is removed. Other backends (CoreMIDI,
// JACK: later) add nothing, so a trailing number there is part of the name.
enum class MidiNaming { WinMM, Alsa, Plain };
MidiNaming nativeMidiNaming();

// The name without the number this backend adds (unchanged for Plain, or
// when the name does not end in that backend's kind of number).
std::string midiPortBaseName(std::string_view name, MidiNaming naming = nativeMidiNaming());

// One of mini-daw's own RtMidi ports (ALSA lists them as
// "RtMidi Input Client:mini-daw 130:0" / "RtMidi Output Client:mini-daw ...").
// They are left out of the port lists: choosing one would loop MIDI back in.
bool isOwnMidiPort(std::string_view name);

// The port in `available` to use for `wanted`: the same name, else the only
// port with the same base name. Empty if none (or several) match.
std::optional<std::size_t> matchMidiPort(const std::vector<std::string>& available, const std::string& wanted,
                                         MidiNaming naming = nativeMidiNaming());

// What checkConnections() decides, as a pure function (unit-tested,
// crosscheck #5 K1). Ports are checked per base name by count: they stay
// connected while at least as many ports with that base name are present
// (the open handles stay valid when the system renumbers them). When the
// count drops and several open ports share the base name, it cannot be told
// which device went: all of them are closed and blocked (not reopened
// automatically), so a track never silently records the other device.
struct MidiPortState {
    std::string name;          // the name the track uses
    bool connected = false;
    std::string openedAs;      // when connected: the name it was opened as
    bool blocked = false;      // after an ambiguous unplug: wait for the user
};
struct MidiCheckPlan {
    std::vector<std::size_t> disconnect;                          // gone
    std::vector<std::size_t> block;                               // gone or not: cannot tell (also disconnect)
    std::vector<std::pair<std::size_t, std::string>> rename;      // still there, new name
    std::vector<std::pair<std::size_t, std::string>> connect;     // open as this available name
};
MidiCheckPlan planMidiCheck(const std::vector<MidiPortState>& ports, const std::vector<std::string>& available,
                            MidiNaming naming = nativeMidiNaming());

// A change found by MidiInputs::checkConnections().
struct MidiPortEvent {
    enum class Kind { Opened, Disconnected, Reconnected, Failed, Ambiguous } kind = Kind::Opened;
    std::string port;      // the name the track uses
    std::string detail;    // the name it was opened as, or the error
};

class MidiInputs : public SessionMidi {
public:
    MidiInputs();
    ~MidiInputs() override;
    MidiInputs(const MidiInputs&) = delete;
    MidiInputs& operator=(const MidiInputs&) = delete;

    // Make exactly these ports (by name) wanted: opens new ones, closes and
    // forgets the rest. A wanted port that cannot be opened now is kept as
    // "not connected" and opened by a later checkConnections(). Returns what
    // happened to the new ports. Not while recording.
    std::vector<MidiPortEvent> setPorts(const std::vector<std::string>& names);
    void closeAll() { setPorts({}); }

    // Re-read the port list; close ports that have gone, open wanted ports
    // that are (back) there. Also while recording. GUI thread. A failure is
    // reported once, not at every check.
    std::vector<MidiPortEvent> checkConnections();

    // The current MIDI input port names (RtMidi numbering), as for the GUI list.
    std::vector<std::string> availablePorts();

    // Wanted and connected.
    bool isOpen(const std::string& name) const override;
    // Closed after an ambiguous unplug of identical devices; the user has to
    // choose the port again (arm the track off and on).
    bool isBlocked(const std::string& name) const;
    // Wanted (setPorts), connected or not.
    bool isWanted(const std::string& name) const { return find(name) != nullptr; }
    // Times the port went away since it was first wanted (SessionMidi).
    std::uint64_t disconnects(const std::string& name) const override;

    // Capture on: messages are queued for read(). Turning it on first
    // discards anything still queued.
    void setCapture(bool on) override;

    // Session thread: take up to `max` queued messages of a port.
    std::size_t read(const std::string& name, RawMidi* out, std::size_t max) override;

    // Messages seen on a port since it was opened (for an activity display),
    // and messages lost because its ring was full.
    std::uint64_t messagesSeen(const std::string& name) const;
    std::uint64_t messagesLost(const std::string& name) const override;

    // Errors reported by RtMidi while ports are open.
    std::vector<std::string> takeMessages();

    // Live MIDI taps (P10b), GUI thread: every channel message arriving on the
    // port (by name; also after it is reopened) is written to `ring` too, from
    // the port's input thread. At most 8 per port. removeTap returns only when
    // the input thread no longer writes to the ring.
    bool addTap(const std::string& port, SpscRing<RawMidi>* ring);
    void removeTap(const std::string& port, SpscRing<RawMidi>* ring);

private:
    struct Port {
        std::string name;
        SpscRing<RawMidi> ring{4096};
        const std::atomic<bool>* capture = nullptr;
        std::atomic<std::uint64_t> seen{0};
        std::atomic<std::uint64_t> lost{0};
        std::atomic<std::uint64_t> disconnects{0};
        std::atomic<bool> connected{false};
        // Live MIDI for instruments (P10b): every channel message is also
        // written to these rings (producer: this port's input thread).
        std::array<std::atomic<SpscRing<RawMidi>*>, 8> taps{};
        std::atomic<int> inCallback{0};  // the input thread is in onMessage (tap removal waits)
        bool everConnected = false;     // GUI thread only
        std::string openedAs;           // GUI thread only: RtMidi name it was opened as
        std::string lastProblem;        // GUI thread only: last failure reported
        bool blocked = false;           // GUI thread only: ambiguous unplug, not reopened (K1)
        // Last, so it is destroyed first: its thread stops before the ring goes.
        // Only the GUI thread touches it.
        std::unique_ptr<rt::midi::RtMidiIn> in;
    };
    static void onMessage(double deltaTime, std::vector<unsigned char>* message, void* user);
    Port* find(const std::string& name) const;
    // Opens `port` as available[index]; returns an error text, empty on success.
    std::string connect(Port& port, const std::string& actualName);
    void disconnect(Port& port);
    rt::midi::RtMidiIn* scanner(std::string& error);

    std::unique_ptr<rt::midi::RtMidiIn> scanner_;  // only lists ports (GUI thread)
    std::map<std::string, std::vector<SpscRing<RawMidi>*>> tapsByPort_;  // GUI thread
    void applyTaps(Port& port);

    std::vector<std::unique_ptr<Port>> ports_;
    std::atomic<bool> capture_{false};
    std::mutex messagesMutex_;
    std::vector<std::string> messages_;
};

} // namespace minidaw
