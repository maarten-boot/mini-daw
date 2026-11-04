#pragma once

// The instruments the audio thread plays (SPEC-VST-02; P10b).
//
// One slot per MIDI track with an instrument. A slot gets MIDI from two
// places: live, from the track's input port (a tap on MidiInputs, filtered by
// the track's channel; placed at the start of the next block), and during
// playback from the track's take (its events on the timeline; placed at their
// exact sample in the block). The instrument's stereo output is mixed into
// the output with the track's gain (volume, mute, solo).
//
// The GUI changes the slot list by building a new configuration and
// publishing it; the audio thread protects the one it uses with a hazard
// pointer, so the GUI frees an old one only when the audio thread has left
// it. Instruments are loaded and unloaded by the GUI, never while a published
// configuration uses them (detach first).
//
// Per instrument the audio thread keeps the notes it has started (from the
// take and live) and its current gain; this follows the instrument when it
// moves to another slot (crosscheck #9, Q3). Notes are released when playback
// stops and when live input ends (the track is disarmed).

#include "minidaw/instrument.hpp"
#include "minidaw/midi_input.hpp"
#include "minidaw/smf.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace minidaw {

class InstrumentRack {
public:
    static constexpr std::size_t kMaxSlots = 16;
    static constexpr std::size_t kMaxEventsPerBlock = 512;
    static constexpr std::uint32_t kChunk = 8192;  // scratch size; larger blocks in parts

    struct SlotSetup {
        Vst3Instrument* instrument = nullptr;      // owned by the GUI side
        int track = 0;                             // track number (gains)
        int liveChannel = 0;                       // 0 = all, 1..16: only that channel
        bool live = false;                         // take live MIDI from the ring
        std::shared_ptr<const std::vector<MidiEvent>> playback;  // the take's events (file frames)
        std::int64_t takeStart = 0;                // timeline frame of the take's start
        float gain = 1.0f;                         // the track's gain now (volume, mute, solo)
    };

    InstrumentRack();
    ~InstrumentRack();
    InstrumentRack(const InstrumentRack&) = delete;
    InstrumentRack& operator=(const InstrumentRack&) = delete;

    // GUI thread. Replaces the whole slot list (at most kMaxSlots); returns
    // when the audio thread no longer uses the previous list. Slot i's live
    // MIDI ring is liveRing(i).
    void configure(const std::vector<SlotSetup>& slots);
    void clear() { configure({}); }
    SpscRing<RawMidi>* liveRing(std::size_t slot);
    void setGain(int track, float gain);

    // Audio thread: adds every instrument's output to `out` (interleaved, 1
    // or 2 channels). `timeline`: the block's timeline position while playing.
    void process(float* out, unsigned int frames, unsigned int outChannels, std::optional<std::uint64_t> timeline);

private:
    struct Config;
    // Audio thread only: the state of one instrument (found by instrument and load serial).
    struct Voices {
        const Vst3Instrument* owner = nullptr;
        std::uint64_t serial = 0;
        std::array<std::uint16_t, 128> heldPlay{};  // notes the playback started (bit per channel)
        std::array<std::uint16_t, 128> heldLive{};  // notes live input started
        bool wasPlaying = false;
        bool wasLive = false;
        float gain = 1.0f;                           // gain at the end of the last block (ramps)
    };
    Voices& voicesFor(const Config& c, const SlotSetup& s, std::size_t slot);

    std::atomic<Config*> config_{nullptr};
    std::atomic<Config*> inUse_{nullptr};
    std::unique_ptr<Config> owned_;
    std::array<std::unique_ptr<SpscRing<RawMidi>>, kMaxSlots> live_;
    std::array<Voices, kMaxSlots> voices_{};         // audio thread only
    std::vector<float> scratchL_, scratchR_;
};

} // namespace minidaw
