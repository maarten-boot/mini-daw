#include "minidaw/instrument_rack.hpp"

#include <algorithm>
#include <thread>

namespace minidaw {

struct InstrumentRack::Config {
    std::vector<SlotSetup> slots;
};

InstrumentRack::InstrumentRack()
{
    for (auto& r : live_) r = std::make_unique<SpscRing<RawMidi>>(1024);
    for (auto& g : gains_) g.store(1.0f);
    curGain_.fill(1.0f);
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
        for (std::size_t i = 0; i < next->slots.size(); ++i) slotTrack_[i] = next->slots[i].track;
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
        if (slotTrack_[i] == track) gains_[i].store(gain, std::memory_order_relaxed);
    }
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

    InstrumentMidi events[kMaxEventsPerBlock];
    for (std::size_t i = 0; i < c->slots.size(); ++i) {
        const SlotSetup& s = c->slots[i];
        if (s.instrument == nullptr || !s.instrument->loaded()) continue;
        std::size_t n = 0;
        const bool playing = timeline.has_value();

        // Playback stopped (or a new position): release the notes it started.
        if (wasPlaying_[i] && !playing) {
            for (int note = 0; note < 128; ++note) {
                for (int ch = 0; ch < 16 && n < kMaxEventsPerBlock; ++ch) {
                    if (held_[i][note] & (1u << ch)) {
                        events[n++] = InstrumentMidi{0, {static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(note), 0}, 3};
                    }
                }
                held_[i][note] = 0;
            }
        }
        wasPlaying_[i] = playing;

        // Live MIDI: at the start of the block (lowest latency).
        if (s.live) {
            RawMidi m;
            while (n < kMaxEventsPerBlock && live_[i]->read(&m, 1) == 1) {
                const int ch = m.data[0] & 0x0F;
                if (s.liveChannel != 0 && ch != s.liveChannel - 1) continue;
                InstrumentMidi e{0, {m.data[0], m.data[1], m.data[2]}, m.size};
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
                for (; it != ev.end() && static_cast<std::int64_t>(it->frame) < to && n < kMaxEventsPerBlock; ++it) {
                    InstrumentMidi e{static_cast<std::uint32_t>(static_cast<std::int64_t>(it->frame) - from),
                                     {it->data[0], it->data[1], it->data[2]}, it->size};
                    const std::uint8_t status = e.data[0] & 0xF0;
                    const int ch = e.data[0] & 0x0F;
                    if (status == 0x90 && e.data[2] > 0) held_[i][e.data[1] & 127] |= static_cast<std::uint16_t>(1u << ch);
                    if (status == 0x80 || (status == 0x90 && e.data[2] == 0)) {
                        held_[i][e.data[1] & 127] &= static_cast<std::uint16_t>(~(1u << ch));
                    }
                    events[n++] = e;
                }
            }
        }
        // Live events (offset 0) come first, playback events after in time order.
        std::stable_sort(events, events + n,
                         [](const InstrumentMidi& a, const InstrumentMidi& b) { return a.offset < b.offset; });

        // Process in parts of the scratch size; mix with the track's gain (ramped).
        const float g1 = gains_[i].load(std::memory_order_relaxed);
        const float g0 = curGain_[i];
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
        curGain_[i] = g1;
    }
    inUse_.store(nullptr, std::memory_order_release);
}

} // namespace minidaw
