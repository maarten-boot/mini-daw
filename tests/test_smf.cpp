// MIDI recording (SPEC-REC-04, 04a-c): T-REC-16, T-REC-17, T-REC-18, T-REC-20.

#include "minidaw/frame_clock.hpp"
#include "minidaw/smf.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <atomic>
#include <thread>

using namespace minidaw;

namespace {

MidiEvent ev(std::uint64_t frame, std::uint8_t a, std::uint8_t b, std::uint8_t c = 0, std::uint8_t size = 3)
{
    MidiEvent e;
    e.frame = frame;
    e.data = {a, b, c};
    e.size = size;
    return e;
}

TimedMidi timed(std::uint64_t frame, std::uint8_t a, std::uint8_t b, std::uint8_t c = 0, std::uint8_t size = 3)
{
    return {frame, {a, b, c}, size};
}

const SmfReadEvent* findMeta(const std::vector<SmfReadEvent>& t, std::uint8_t type)
{
    for (const auto& e : t) {
        if (e.meta && e.metaType == type) return &e;
    }
    return nullptr;
}

std::vector<SmfReadEvent> channelEvents(const std::vector<SmfReadEvent>& t)
{
    std::vector<SmfReadEvent> out;
    for (const auto& e : t) {
        if (!e.meta) out.push_back(e);
    }
    return out;
}

} // namespace

TEST_CASE("T-REC-16: frames to ticks", "[smf][T-REC-16]")
{
    CHECK(framesToTicks(48000, 48000, MidiTiming::Ppq) == 1920);   // 1 s = 2 quarters at 120 BPM
    CHECK(framesToTicks(48000, 48000, MidiTiming::Smpte) == 1000);  // 1 s = 1000 ms
    CHECK(framesToTicks(25, 48000, MidiTiming::Ppq) == 1);          // 0.52 ms
    CHECK(framesToTicks(12, 48000, MidiTiming::Ppq) == 0);
    CHECK(framesToTicks(44100 * 60, 44100, MidiTiming::Smpte) == 60000);
    CHECK(framesToTicks(1, 0, MidiTiming::Ppq) == 0);
}

TEST_CASE("T-REC-16: SMF type 1 written and read back, ppq and smpte", "[smf][T-REC-16]")
{
    test::TempDir dir;
    const auto timing = GENERATE(MidiTiming::Ppq, MidiTiming::Smpte);
    const auto path = dir / "take.mid";
    SmfTrack track;
    track.name = "Keys";
    track.events = {ev(0, 0x90, 60, 100), ev(24000, 0x80, 60, 0), ev(24000, 0xB0, 64, 127),
                    ev(48000, 0xC0, 5, 0, 2), ev(96000, 0xE0, 0, 64)};
    std::string err;
    REQUIRE(writeSmf(path, "take-1", {track}, 48000, timing, 144000, true, err));
    CHECK_FALSE(std::filesystem::exists(dir / "take.mid.tmp"));

    SmfData d;
    REQUIRE(readSmf(path, d, err));
    CHECK(d.format == 1);
    REQUIRE(d.tracks.size() == 2);
    const double tps = timing == MidiTiming::Ppq ? 1920.0 : 1000.0;
    if (timing == MidiTiming::Ppq) {
        CHECK(d.division == 960);
        const auto* tempo = findMeta(d.tracks[0], 0x51);
        REQUIRE(tempo != nullptr);
        CHECK(tempo->bytes == std::vector<std::uint8_t>{0x07, 0xA1, 0x20});  // 500000 us
    } else {
        CHECK(static_cast<std::uint16_t>(d.division) == 0xE728);              // -25 fps, 40 sub-frames
        CHECK(findMeta(d.tracks[0], 0x51) == nullptr);
    }
    const auto* name = findMeta(d.tracks[1], 0x03);
    REQUIRE(name != nullptr);
    CHECK(std::string(name->bytes.begin(), name->bytes.end()) == "Keys");

    const auto evs = channelEvents(d.tracks[1]);
    REQUIRE(evs.size() == 5);
    CHECK(evs[0].tick == 0);
    CHECK(evs[0].bytes == std::vector<std::uint8_t>{0x90, 60, 100});
    CHECK(evs[1].tick == static_cast<std::uint32_t>(0.5 * tps));
    CHECK(evs[2].bytes == std::vector<std::uint8_t>{0xB0, 64, 127});
    CHECK(evs[3].tick == static_cast<std::uint32_t>(1.0 * tps));
    CHECK(evs[3].bytes == std::vector<std::uint8_t>{0xC0, 5});
    CHECK(evs[4].tick == static_cast<std::uint32_t>(2.0 * tps));
    // End of track at the end of the take (3 s) in both tracks.
    CHECK(findMeta(d.tracks[0], 0x2F)->tick == static_cast<std::uint32_t>(3.0 * tps));
    CHECK(findMeta(d.tracks[1], 0x2F)->tick == static_cast<std::uint32_t>(3.0 * tps));
}

TEST_CASE("T-REC-17: building a MIDI take", "[smf][T-REC-17]")
{
    const std::vector<TimedMidi> in = {
        timed(900, 0x91, 50, 90),    // before the start: dropped
        timed(1000, 0x91, 60, 100),  // ch 2 note on
        timed(1100, 0x90, 61, 100),  // ch 1: filtered out
        timed(1500, 0x91, 60, 0),    // ch 2 note off (velocity 0)
        timed(1600, 0x91, 62, 80),   // ch 2 note on, still held at the end
        timed(1700, 0xF8, 0, 0, 1),  // clock: not recorded
        timed(1800, 0xB1, 7, 100),   // ch 2 controller
        timed(5000, 0x91, 70, 100),  // after the end: dropped
    };
    const auto out = buildMidiTake(in, 2, 1000, 3000);
    REQUIRE(out.size() == 5);
    CHECK(out[0].frame == 0);
    CHECK(out[0].data[1] == 60);
    CHECK(out[1].frame == 500);
    CHECK(out[2].frame == 600);
    CHECK(out[3].frame == 800);
    CHECK(out[3].data[0] == 0xB1);
    CHECK(out[4].frame == 2000);  // added note-off for note 62 at the end
    CHECK(out[4].data[0] == 0x81);
    CHECK(out[4].data[1] == 62);

    const auto all = buildMidiTake(in, 0, 1000, 3000);
    CHECK(all.size() == 7);  // + ch 1 note on and its added note-off
}

TEST_CASE("T-REC-18: clock time to frame", "[smf][T-REC-18]")
{
    CHECK(FrameClock::mapToFrame(48000, 1'000'000'000, 1'010'000'000, 48000) == 48480);  // +10 ms
    CHECK(FrameClock::mapToFrame(48000, 1'000'000'000, 990'000'000, 48000) == 47520);    // -10 ms
    CHECK(FrameClock::mapToFrame(100, 1'000'000'000, 0, 48000) == 0);                    // clamped

    FrameClock clock;
    std::uint64_t f = 0;
    std::int64_t t = 0;
    CHECK_FALSE(clock.read(f, t));
    CHECK_FALSE(clock.frameAt(0, 48000).has_value());
    clock.publish(256, 5'000'000);
    CHECK(clock.frameAt(5'000'000, 48000) == 256u);
}

TEST_CASE("T-REC-18: the frame clock never shows a half-written pair", "[smf][T-REC-18]")
{
    FrameClock clock;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (std::uint64_t i = 1; i < 2'000'000; ++i) clock.publish(i, static_cast<std::int64_t>(i * 3));
        stop = true;
    });
    std::size_t bad = 0;
    std::size_t reads = 0;
    while (!stop) {
        std::uint64_t f = 0;
        std::int64_t t = 0;
        if (clock.read(f, t)) {
            ++reads;
            if (t != static_cast<std::int64_t>(f * 3)) ++bad;
        }
    }
    writer.join();
    CHECK(bad == 0);
    CHECK(reads > 0);
}

TEST_CASE("T-REC-20: a MIDI take rewritten while recording stays valid", "[smf][T-REC-20]")
{
    test::TempDir dir;
    const auto path = dir / "live.mid";
    SmfTrack track;
    track.name = "Live";
    track.events = {ev(0, 0x90, 60, 100)};
    std::string err;
    REQUIRE(writeSmf(path, "t", {track}, 48000, MidiTiming::Ppq, 48000, false, err));
    track.events.push_back(ev(48000, 0x80, 60, 0));
    track.events.push_back(ev(72000, 0x90, 64, 100));
    REQUIRE(writeSmf(path, "t", {track}, 48000, MidiTiming::Ppq, 96000, false, err));  // replaces the file
    SmfData d;
    REQUIRE(readSmf(path, d, err));
    CHECK(channelEvents(d.tracks[1]).size() == 3);
    CHECK_FALSE(std::filesystem::exists(dir / "live.mid.tmp"));

    CHECK_FALSE(writeSmf(dir / "no-such-folder" / "x.mid", "t", {track}, 48000, MidiTiming::Ppq, 0, false, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("MIDI timing names", "[smf][T-REC-22]")
{
    MidiTiming t = MidiTiming::Ppq;
    CHECK(parseMidiTiming("SMPTE", t));
    CHECK(t == MidiTiming::Smpte);
    CHECK(parseMidiTiming("ppq", t));
    CHECK(t == MidiTiming::Ppq);
    CHECK_FALSE(parseMidiTiming("bpm", t));
    CHECK(std::string(midiTimingName(MidiTiming::Smpte)) == "smpte");
}

TEST_CASE("T-REC-18: the clock smoother removes callback jitter", "[smf][T-REC-18]")
{
    // 48 kHz, 1024-frame blocks; callbacks late by 0..15 ms (fixed pseudo-random),
    // with a start-up burst of 4 blocks delivered at once.
    const unsigned int rate = 48000;
    const unsigned int block = 1024;
    const std::int64_t t0 = 5'000'000'000;
    ClockSmoother sm;
    std::uint32_t seed = 12345;
    auto lateNs = [&] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<std::int64_t>((seed >> 8) % 15'000'000);
    };
    std::int64_t worst = 0;
    for (std::uint64_t i = 1; i <= 600; ++i) {  // about 12.8 s
        const std::uint64_t frame = i * block;
        const std::int64_t ideal = t0 + framesToNs(frame, rate);
        const std::int64_t callback = i <= 4 ? t0 + framesToNs(4 * block, rate) - 30'000'000 : ideal + lateNs();
        const std::int64_t smoothed = sm.update(frame, callback, rate, block, false);
        if (i > 70) worst = std::max(worst, std::abs(smoothed - ideal));  // from 1.5 s on
    }
    CAPTURE(worst);
    CHECK(worst < 2'000'000);  // < 2 ms from up to 15 ms uniform raw jitter (live: about 0.5 ms)
}

TEST_CASE("T-REC-18: the clock smoother follows drift and restarts after a dropout", "[smf][T-REC-18]")
{
    const unsigned int rate = 48000;
    const unsigned int block = 256;
    ClockSmoother sm;
    // The audio clock runs 300 ppm slow: each block takes a little longer in system time.
    std::int64_t worst = 0;
    std::int64_t gap = 0;
    for (std::uint64_t i = 1; i <= 20000; ++i) {  // about 107 s
        const std::uint64_t frame = i * block;
        const auto ideal = static_cast<std::int64_t>(static_cast<double>(framesToNs(frame, rate)) * 1.0003);
        const bool dropout = i == 15000;
        if (dropout) gap = 100'000'000;  // 100 ms of time without frames
        const std::int64_t t = ideal + gap;
        const std::int64_t smoothed = sm.update(frame, t, rate, block, dropout);
        if (i > 1000 && (i < 15000 || i > 15001)) worst = std::max(worst, std::abs(smoothed - t));
    }
    CHECK(worst < 1'000'000);
    CHECK(framesToNs(48000ull * 3600 * 100, 48000) == 360'000'000'000'000);  // 100 h, no overflow
}
