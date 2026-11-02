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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class RtMidiIn;

namespace minidaw {

struct RawMidi {
    std::int64_t ns = 0;                     // steadyNowNs() when it arrived
    std::array<std::uint8_t, 3> data{};
    std::uint8_t size = 0;
};

// RtMidi adds a number to port names: Windows " 0" (the device index), ALSA
// " 24:0" (client:port). Both can change when a device is plugged in again.
// This returns the name without that number.
std::string midiPortBaseName(std::string_view name);

// The port in `available` to use for `wanted`: the same name, else the only
// port with the same base name. Empty if none (or several) match.
std::optional<std::size_t> matchMidiPort(const std::vector<std::string>& available, const std::string& wanted);

// A change found by MidiInputs::checkConnections().
struct MidiPortEvent {
    enum class Kind { Opened, Disconnected, Reconnected, Failed } kind = Kind::Opened;
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

private:
    struct Port {
        std::string name;
        SpscRing<RawMidi> ring{4096};
        const std::atomic<bool>* capture = nullptr;
        std::atomic<std::uint64_t> seen{0};
        std::atomic<std::uint64_t> lost{0};
        std::atomic<std::uint64_t> disconnects{0};
        std::atomic<bool> connected{false};
        bool everConnected = false;     // GUI thread only
        std::string openedAs;           // GUI thread only: RtMidi name it was opened as
        std::string lastProblem;        // GUI thread only: last failure reported
        // Last, so it is destroyed first: its thread stops before the ring goes.
        // Only the GUI thread touches it.
        std::unique_ptr<RtMidiIn> in;
    };
    static void onMessage(double deltaTime, std::vector<unsigned char>* message, void* user);
    Port* find(const std::string& name) const;
    // Opens `port` as available[index]; returns an error text, empty on success.
    std::string connect(Port& port, const std::string& actualName);
    void disconnect(Port& port);
    RtMidiIn* scanner(std::string& error);

    std::unique_ptr<RtMidiIn> scanner_;  // only lists ports (GUI thread)

    std::vector<std::unique_ptr<Port>> ports_;
    std::atomic<bool> capture_{false};
    std::mutex messagesMutex_;
    std::vector<std::string> messages_;
};

} // namespace minidaw
