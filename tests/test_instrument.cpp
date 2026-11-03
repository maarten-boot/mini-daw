// A VST3 instrument loaded and played (SPEC-VST-02, 06), with the test synth.

#include "minidaw/instrument.hpp"
#include "minidaw/plugin_scan.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

using namespace minidaw;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

std::string testSynthClass()
{
    const ScannedModule m = scanModule(MINIDAW_TEST_VST3);
    REQUIRE(m.classes.size() == 1);
    return m.classes[0].classId;
}

float peak(const std::vector<float>& v, std::size_t from, std::size_t to)
{
    float p = 0;
    for (std::size_t i = from; i < to; ++i) p = std::max(p, std::fabs(v[i]));
    return p;
}

// Frequency from upward zero crossings.
double frequency(const std::vector<float>& v, std::size_t from, std::size_t to, double rate)
{
    int crossings = 0;
    std::size_t first = 0, last = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
        if (v[i - 1] <= 0 && v[i] > 0) {
            if (crossings == 0) first = i;
            last = i;
            ++crossings;
        }
    }
    return crossings > 1 ? (crossings - 1) * rate / static_cast<double>(last - first) : 0.0;
}

InstrumentMidi msg(std::uint32_t offset, std::uint8_t a, std::uint8_t b, std::uint8_t c)
{
    return InstrumentMidi{offset, {a, b, c}, 3};
}

} // namespace

TEST_CASE("T-VST-12: the test synth is loaded and plays notes sample-accurately", "[vst][T-VST-12]")
{
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    INFO(err);
    CHECK(inst.loaded());
    CHECK(inst.name() == "mini-daw Test Synth");
    REQUIRE(inst.controller() != nullptr);

    std::vector<float> l(2048), r(2048);
    const InstrumentMidi on = msg(100, 0x90, 69, 127);  // A4, full velocity
    inst.process(&on, 1, 2048, {}, l.data(), r.data());
    CHECK(peak(l, 0, 100) == 0.0f);                    // nothing before the note
    CHECK(l[101] != 0.0f);                              // from its sample offset on
    CHECK(peak(l, 100, 2048) == Approx(0.5f).margin(0.01));  // gain 0.5 x velocity 1
    CHECK(frequency(l, 100, 2048, 48000) == Approx(440.0).epsilon(0.01));
    CHECK(r == l);                                      // stereo

    // A parameter change from the host: gain 1.0 from the next block.
    inst.setParameter(1, 1.0);
    inst.process(nullptr, 0, 2048, {}, l.data(), r.data());
    CHECK(peak(l, 0, 2048) == Approx(1.0f).margin(0.01));
    inst.idle();  // passes the change on to the controller

    // State: saved and restored.
    std::string comp, ctrl;
    REQUIRE(inst.getState(comp, ctrl, err));
    REQUIRE(comp.size() == 8);  // the gain, a double
    double g = 0.25;
    std::string quarter(reinterpret_cast<const char*>(&g), 8);  // little-endian, as the synth writes it
    REQUIRE(inst.setState(quarter, ctrl, err));
    inst.process(nullptr, 0, 2048, {}, l.data(), r.data());
    CHECK(peak(l, 0, 2048) == Approx(0.25f).margin(0.01));
    REQUIRE(inst.setState(comp, ctrl, err));  // back to 1.0
    inst.process(nullptr, 0, 512, {}, l.data(), r.data());
    CHECK(peak(l, 0, 512) == Approx(1.0f).margin(0.01));

    // Note off: silence from that sample on.
    const InstrumentMidi off = msg(300, 0x80, 69, 0);
    inst.process(&off, 1, 1024, {}, l.data(), r.data());
    CHECK(peak(l, 0, 300) > 0.5f);
    CHECK(peak(l, 301, 1024) == 0.0f);

    inst.unload();
    CHECK_FALSE(inst.loaded());
}

TEST_CASE("T-VST-13: blocks larger than the plugin block; bad class; reload", "[vst][T-VST-13]")
{
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 44100, err));
    const std::uint32_t n = Vst3Instrument::kMaxBlock + 1500;
    std::vector<float> l(n), r(n);
    const InstrumentMidi on = msg(Vst3Instrument::kMaxBlock + 200, 0x90, 81, 127);  // A5, in the second part
    inst.process(&on, 1, n, {}, l.data(), r.data());
    CHECK(peak(l, 0, Vst3Instrument::kMaxBlock + 200) == 0.0f);
    CHECK(frequency(l, Vst3Instrument::kMaxBlock + 200, n, 44100) == Approx(880.0).epsilon(0.02));

    Vst3Instrument other;
    CHECK_FALSE(other.load(MINIDAW_TEST_VST3, "00000000000000000000000000000000", 48000, err));
    CHECK(err.find("no class") != std::string::npos);
    CHECK_FALSE(other.load(fs::path(MINIDAW_TEST_VST3) / "missing", testSynthClass(), 48000, err));

    // Unload and load again (the module is unloaded and loaded again).
    inst.unload();
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    CHECK(inst.loaded());
}

#include "minidaw/instrument_rack.hpp"

TEST_CASE("T-VST-14: instrument rack: playback MIDI at the sample, live MIDI, gain, stop", "[vst][T-VST-14]")
{
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    InstrumentRack rack;
    auto take = std::make_shared<std::vector<MidiEvent>>(std::vector<MidiEvent>{
        {100, {0x90, 69, 127}, 3}, {700, {0x80, 69, 0}, 3}});
    InstrumentRack::SlotSetup slot;
    slot.instrument = &inst;
    slot.track = 3;
    slot.playback = take;
    slot.takeStart = 1000;  // the take starts at timeline frame 1000
    rack.configure({slot});

    std::vector<float> out(2 * 512);
    auto run = [&](std::optional<std::uint64_t> timeline) {
        std::fill(out.begin(), out.end(), 0.0f);
        rack.process(out.data(), 512, 2, timeline);
    };
    auto peakOut = [&](unsigned from, unsigned to) {
        float p = 0;
        for (unsigned f = from; f < to; ++f) p = std::max(p, std::fabs(out[f * 2]));
        return p;
    };

    run(std::uint64_t{800});            // timeline 800..1311: the note starts at 1100 (frame 300 of the block)
    CHECK(peakOut(0, 300) == 0.0f);
    CHECK(peakOut(301, 512) == Approx(0.5f).margin(0.01));
    run(std::uint64_t{1312});           // still on until 1700
    CHECK(peakOut(0, 388) > 0.4f);      // 1312 + 388 = 1700: off from there
    CHECK(peakOut(389, 512) == 0.0f);

    // Gain (volume / mute) of the track, ramped over a block.
    run(std::uint64_t{800});
    rack.setGain(3, 0.0f);
    run(std::uint64_t{1312});
    run(std::uint64_t{800});            // fully muted now
    CHECK(peakOut(301, 512) == 0.0f);
    rack.setGain(3, 1.0f);
    run(std::uint64_t{2000});

    // Playback stops while a note is held: it is released.
    run(std::uint64_t{800});            // note on at 1100
    run(std::nullopt);                  // stopped: note-off at once
    CHECK(peakOut(1, 512) == 0.0f);

    // Live MIDI from the ring, with a channel filter.
    slot.playback = nullptr;
    slot.live = true;
    slot.liveChannel = 2;               // only channel 2
    rack.configure({slot});
    RawMidi on1{0, {0x90, 60, 127}, 3}; // channel 1: ignored
    rack.liveRing(0)->write(&on1, 1);
    run(std::nullopt);
    CHECK(peakOut(0, 512) == 0.0f);
    RawMidi on2{0, {0x91, 60, 127}, 3}; // channel 2: played from the start of the block
    rack.liveRing(0)->write(&on2, 1);
    run(std::nullopt);
    CHECK(peakOut(1, 512) == Approx(0.5f).margin(0.01));
    rack.clear();
}
