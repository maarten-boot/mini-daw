#pragma once

// MIDI playback to MIDI output ports (SPEC-PLAY-03, 04).
//
// The events of all playing MIDI tracks are merged into one list on the
// timeline. A sender thread wakes about every millisecond, asks where the
// timeline is as heard now (a function given by the caller: the player's
// position at the current stream frame minus the output latency estimate)
// and sends every event up to there. Events before the start position are
// skipped (no chasing of controllers yet). A muted track sends no new notes
// and its held notes are released. Stop releases every held note and sends
// "all notes off" on the channels used. On Windows the timer resolution is
// raised to 1 ms while playing.

#include "minidaw/smf.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace minidaw {

// Where MIDI goes; RtMidi by default, a stand-in in the tests.
class MidiOutputSink {
public:
    virtual ~MidiOutputSink() = default;
    virtual std::vector<std::string> ports() = 0;
    // Opens an output (by RtMidi name); returns its handle, or -1 with `error`.
    virtual int open(const std::string& name, std::string& error) = 0;
    virtual void send(int handle, const std::uint8_t* data, std::size_t size) = 0;
    virtual void closeAll() = 0;
};
std::unique_ptr<MidiOutputSink> makeRtMidiOutputSink();

struct MidiPlaybackTrack {
    int track = 0;                    // track number (for mute/solo/volume)
    std::string port;                 // MIDI output port name
    std::vector<MidiEvent> events;    // frames relative to the file start
    std::int64_t start = 0;           // timeline frame of the file start
};

class MidiPlayer {
public:
    using Clock = std::function<std::optional<std::int64_t>()>;  // timeline frame heard now

    explicit MidiPlayer(std::unique_ptr<MidiOutputSink> sink = makeRtMidiOutputSink());
    ~MidiPlayer();
    MidiPlayer(const MidiPlayer&) = delete;
    MidiPlayer& operator=(const MidiPlayer&) = delete;

    // GUI thread. Opens the ports (a port that does not open: warning, its
    // track is not played) and starts sending from `position`.
    void start(const std::vector<MidiPlaybackTrack>& tracks, std::int64_t position, Clock clock,
               std::vector<std::string>& warnings);
    void stop();
    bool playing() const { return thread_.joinable() || testMode_; }

    // 0 = silent (mute, not soloed); else velocities are scaled (1 = as recorded).
    void setGain(int track, float gain);
    std::uint64_t sent() const { return sent_.load(std::memory_order_relaxed); }
    // Tracks that play (their output opened) since the last start().
    std::size_t trackCount() const { return gains_.size(); }
    std::vector<std::string> outputPorts() { return sink_->ports(); }

    // Tests: no thread; step() sends what is due at `now`.
    void startForTest(const std::vector<MidiPlaybackTrack>& tracks, std::int64_t position,
                      std::vector<std::string>& warnings);
    void step(std::int64_t now);

private:
    struct Item {
        std::int64_t frame = 0;
        int handle = -1;
        int track = 0;
        std::array<std::uint8_t, 3> data{};
        std::uint8_t size = 0;
    };
    void prepare(const std::vector<MidiPlaybackTrack>& tracks, std::int64_t position,
                 std::vector<std::string>& warnings);
    void loop();
    void sendItem(const Item& it);
    void releaseTrack(int track);   // note-offs for its held notes
    void releaseAll();

    std::unique_ptr<MidiOutputSink> sink_;
    Clock clock_;
    std::vector<Item> items_;
    std::size_t next_ = 0;
    std::map<int, std::atomic<float>*> gains_;           // per track (GUI writes, sender reads)
    std::vector<std::unique_ptr<std::atomic<float>>> gainStore_;
    std::map<int, float> lastGain_;                      // sender: to notice a mute
    std::map<int, float> requested_;                     // GUI: gains for the next start
    // Held notes: (handle, channel, note, track); per track, so two tracks on
    // one port and channel do not release each other's notes (M8).
    std::set<std::tuple<int, int, int, int>> held_;
    std::map<int, std::uint16_t> channelsUsed_;          // handle -> channel bits
    std::thread thread_;
    std::atomic<bool> run_{false};
    std::atomic<std::uint64_t> sent_{0};
    bool testMode_ = false;
};

} // namespace minidaw
