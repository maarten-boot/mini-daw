#include "minidaw/instrument_rack.hpp"

#include <algorithm>
#include <thread>

namespace minidaw {

struct InstrumentRack::Config {
    std::vector<SlotSetup> slots;
    std::array<std::atomic<float>, kMaxSlots> gains{};  // per slot; setGain changes them
};

namespace {

void noteOff(InstrumentMidi* events, std::size_t& n, int ch, int note)
{
    events[n++] = InstrumentMidi{0, {static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(note), 0}, 3};
}

// Note-offs at offset 0 for every note in `held`, which is then cleared.
void releaseAll(std::array<std::uint16_t, 128>& held, InstrumentMidi* events, std::size_t& n, std::size_t cap)
{
    for (int note = 0; note < 128; ++note) {
        if (held[note] == 0) continue;
        for (int ch = 0; ch < 16 && n < cap; ++ch) {
            if (held[note] & (1u << ch)) noteOff(events, n, ch, note);
        }
        held[note] = 0;
    }
}

bool isNoteOff(const InstrumentMidi& e)
{
    const std::uint8_t status = e.data[0] & 0xF0;
    return status == 0x80 || (status == 0x90 && e.data[2] == 0);
}

bool isNoteOn(const InstrumentMidi& e)
{
    return (e.data[0] & 0xF0) == 0x90 && e.data[2] > 0;
}

void track(std::array<std::uint16_t, 128>& held, const InstrumentMidi& e, bool add)
{
    const auto bit = static_cast<std::uint16_t>(1u << (e.data[0] & 0x0F));
    if (add) {
        held[e.data[1] & 127] |= bit;
    } else {
        held[e.data[1] & 127] &= static_cast<std::uint16_t>(~bit);
    }
}

} // namespace

InstrumentRack::InstrumentRack()
{
    for (auto& r : live_) r = std::make_unique<SpscRing<RawMidi>>(1024);
    scratchL_.resize(kChunk);
    scratchR_.resize(kChunk);
}

InstrumentRack::~InstrumentRack()
{
    clear();
}

SpscRing<RawMidi>* InstrumentRack::liveRing(std::size_t slot)
{
    return slot < kMaxSlots ? live_[slot].get() : nullptr;
}

void InstrumentRack::configure(const std::vector<SlotSetup>& slots)
{
    std::unique_ptr<Config> next;
    if (!slots.empty()) {
        next = std::make_unique<Config>();
        next->slots.assign(slots.begin(), slots.begin() + static_cast<std::ptrdiff_t>(std::min(slots.size(), kMaxSlots)));
        for (std::size_t i = 0; i < next->slots.size(); ++i) next->gains[i].store(next->slots[i].gain);
    }
    Config* old = config_.exchange(next.get(), std::memory_order_seq_cst);
    // The audio thread holds a configuration for at most one block.
    while (old != nullptr && inUse_.load(std::memory_order_seq_cst) == old) std::this_thread::yield();
    owned_ = std::move(next);
}

void InstrumentRack::setGain(int track, float gain)
{
    if (!owned_) return;
    for (std::size_t i = 0; i < owned_->slots.size(); ++i) {
        if (owned_->slots[i].track == track) owned_->gains[i].store(gain, std::memory_order_relaxed);
    }
}

// Audio thread. The state of the instrument in slot `slot`: where it was
// before (it may have moved to another slot), else a state no instrument of
// this configuration uses, reset.
InstrumentRack::Voices& InstrumentRack::voicesFor(const Config& c, const SlotSetup& s, std::size_t slot)
{
    const std::uint64_t serial = s.instrument->serial();
    for (Voices& v : voices_) {
        if (v.owner == s.instrument && v.serial == serial) return v;
    }
    auto inUse = [&](const Voices& v) {
        if (v.owner == nullptr) return false;
        for (const SlotSetup& o : c.slots) {
            if (o.instrument == v.owner && o.instrument->serial() == v.serial) return true;
        }
        return false;
    };
    Voices* free = nullptr;
    if (!inUse(voices_[slot])) free = &voices_[slot];
    for (std::size_t k = 0; free == nullptr && k < voices_.size(); ++k) {
        if (!inUse(voices_[k])) free = &voices_[k];
    }
    // There are as many states as slots, so one is always free.
    Voices& v = *free;
    v = Voices{};
    v.owner = s.instrument;
    v.serial = serial;
    v.gain = c.gains[slot].load(std::memory_order_relaxed);  // no ramp from another track's gain
    return v;
}

// Audio thread. No locks, no allocation.
void InstrumentRack::process(float* out, unsigned int frames, unsigned int outChannels,
                             std::optional<std::uint64_t> timeline)
{
    Config* c = nullptr;
    do {
        c = config_.load(std::memory_order_seq_cst);
        inUse_.store(c, std::memory_order_seq_cst);
    } while (c != config_.load(std::memory_order_seq_cst));
    if (c == nullptr) return;

    // Events of one block, in time order by construction (no sort: a sort may
    // allocate, crosscheck #9 Q1): note-offs and live input at offset 0 first,
    // then the take's events in their file order.
    InstrumentMidi events[kMaxEventsPerBlock];
    constexpr std::size_t cap = kMaxEventsPerBlock;
    for (std::size_t i = 0; i < c->slots.size(); ++i) {
        const SlotSetup& s = c->slots[i];
        if (s.instrument == nullptr || !s.instrument->loaded()) continue;
        Voices& v = voicesFor(*c, s, i);
        std::size_t n = 0;
        const bool playing = timeline.has_value();

        // Playback stopped: release the notes it started. Live input ended
        // (disarmed): release the notes it started.
        if (v.wasPlaying && !playing) releaseAll(v.heldPlay, events, n, cap);
        v.wasPlaying = playing;
        if (v.wasLive && !s.live) releaseAll(v.heldLive, events, n, cap);
        v.wasLive = s.live;

        // Live MIDI: at the start of the block (lowest latency). What does not
        // fit stays in the ring for the next block.
        if (s.live) {
            RawMidi m;
            while (n < cap && live_[i]->read(&m, 1) == 1) {
                const int ch = m.data[0] & 0x0F;
                if (s.liveChannel != 0 && ch != s.liveChannel - 1) continue;
                const InstrumentMidi e{0, {m.data[0], m.data[1], m.data[2]}, m.size};
                if (isNoteOn(e)) track(v.heldLive, e, true);
                if (isNoteOff(e)) track(v.heldLive, e, false);
                events[n++] = e;
            }
        } else {
            RawMidi drop[16];
            while (live_[i]->read(drop, 16) > 0) {
            }
        }

        // Playback MIDI: the take's events inside this block, at their sample.
        if (playing && s.playback && !s.playback->empty()) {
            const std::int64_t from = static_cast<std::int64_t>(*timeline) - s.takeStart;  // file frames
            const std::int64_t to = from + frames;
            if (to > 0) {
                const auto& ev = *s.playback;
                const std::uint64_t f0 = from > 0 ? static_cast<std::uint64_t>(from) : 0;
                auto it = std::lower_bound(ev.begin(), ev.end(), f0,
                                           [](const MidiEvent& e, std::uint64_t f) { return e.frame < f; });
                for (; it != ev.end() && static_cast<std::int64_t>(it->frame) < to; ++it) {
                    const InstrumentMidi e{static_cast<std::uint32_t>(static_cast<std::int64_t>(it->frame) - from),
                                           {it->data[0], it->data[1], it->data[2]}, it->size};
                    if (n < cap) {
                        if (isNoteOn(e)) track(v.heldPlay, e, true);
                        if (isNoteOff(e)) track(v.heldPlay, e, false);
                        events[n++] = e;
                    } else if (isNoteOff(e) && !isNoteOff(events[n - 1])) {
                        // Full: a note-off takes the place of the last other event
                        // (still in time order), so no note is left hanging.
                        if (isNoteOn(events[n - 1])) track(v.heldPlay, events[n - 1], false);
                        track(v.heldPlay, e, false);
                        events[n - 1] = e;
                    }
                }
            }
        }

        // Process in parts of the scratch size; mix with the track's gain (ramped).
        const float g1 = c->gains[i].load(std::memory_order_relaxed);
        const float g0 = v.gain;
        std::size_t next = 0;
        for (unsigned int done = 0; done < frames;) {
            const unsigned int part = std::min<unsigned int>(frames - done, kChunk);
            std::size_t count = 0;
            InstrumentMidi* first = events + next;
            while (next + count < n && events[next + count].offset < done + part) {
                first[count].offset -= done;
                ++count;
            }
            InstrumentTransport t;
            t.playing = playing;
            t.position = playing ? static_cast<std::int64_t>(*timeline) + done : 0;
            s.instrument->process(first, count, part, t, scratchL_.data(), scratchR_.data());
            next += count;
            if (out != nullptr && outChannels > 0) {
                for (unsigned int f = 0; f < part; ++f) {
                    const float k = static_cast<float>(done + f + 1) / static_cast<float>(frames);
                    const float g = g0 + (g1 - g0) * k;
                    float* o = out + static_cast<std::size_t>(done + f) * outChannels;
                    if (outChannels == 1) {
                        o[0] += 0.5f * g * (scratchL_[f] + scratchR_[f]);
                    } else {
                        o[0] += g * scratchL_[f];
                        o[1] += g * scratchR_[f];
                    }
                }
            }
            done += part;
        }
        v.gain = g1;
    }
    inUse_.store(nullptr, std::memory_order_release);
}

} // namespace minidaw
