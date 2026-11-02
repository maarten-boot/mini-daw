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

#include "minidaw/ring_buffer.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class RtMidiIn;

namespace minidaw {

struct RawMidi {
    std::int64_t ns = 0;                     // steadyNowNs() when it arrived
    std::array<std::uint8_t, 3> data{};
    std::uint8_t size = 0;
};

class MidiInputs {
public:
    MidiInputs();
    ~MidiInputs();
    MidiInputs(const MidiInputs&) = delete;
    MidiInputs& operator=(const MidiInputs&) = delete;

    // Make exactly these ports (by name) open: opens missing ones, closes the
    // rest. Returns a warning per port that is not found or fails to open.
    // Not while recording.
    std::vector<std::string> setPorts(const std::vector<std::string>& names);
    void closeAll() { setPorts({}); }

    bool isOpen(const std::string& name) const;

    // Capture on: messages are queued for read(). Turning it on first
    // discards anything still queued.
    void setCapture(bool on);

    // Session thread: take up to `max` queued messages of a port.
    std::size_t read(const std::string& name, RawMidi* out, std::size_t max);

    // Messages seen on a port since it was opened (for an activity display),
    // and messages lost because its ring was full.
    std::uint64_t messagesSeen(const std::string& name) const;
    std::uint64_t messagesLost(const std::string& name) const;

    // Errors reported by RtMidi while ports are open.
    std::vector<std::string> takeMessages();

private:
    struct Port {
        std::string name;
        SpscRing<RawMidi> ring{4096};
        const std::atomic<bool>* capture = nullptr;
        std::atomic<std::uint64_t> seen{0};
        std::atomic<std::uint64_t> lost{0};
        // Last, so it is destroyed first: its thread stops before the ring goes.
        std::unique_ptr<RtMidiIn> in;
    };
    static void onMessage(double deltaTime, std::vector<unsigned char>* message, void* user);
    Port* find(const std::string& name) const;

    std::vector<std::unique_ptr<Port>> ports_;
    std::atomic<bool> capture_{false};
    std::mutex messagesMutex_;
    std::vector<std::string> messages_;
};

} // namespace minidaw
