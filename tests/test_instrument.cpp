// A VST3 instrument loaded and played (SPEC-VST-02, 06), with the test synth.

#include "minidaw/instrument.hpp"
#include "minidaw/plugin_scan.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/render.hpp"
#include "minidaw/track.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

TEST_CASE("T-VST-18: the plugin editor: create, scale, attach, resize, requests, close", "[vst][T-VST-18]")
{
    Vst3Instrument inst;
    std::string err;
    InstrumentEditor ed;
    CHECK_FALSE(ed.create(inst, "minidaw-test", err));  // nothing loaded
    CHECK_FALSE(err.empty());

    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    err.clear();
    CHECK_FALSE(ed.create(inst, "NoSuchPlatform", err));
    CHECK(err.find("NoSuchPlatform") != std::string::npos);

    std::vector<InstrumentEditor::Size> requests;
    bool accept = true;
    ed.onResizeRequest = [&](InstrumentEditor::Size s) {
        requests.push_back(s);
        return accept;
    };
    REQUIRE(ed.create(inst, "minidaw-test", err));
    CHECK(ed.isOpen());
    InstrumentEditor second;
    CHECK_FALSE(second.create(inst, "minidaw-test", err));  // one editor per instrument
    CHECK(err.find("already open") != std::string::npos);

    ed.setContentScale(1.5f);
    CHECK(inst.controller()->getParamNormalized(2) == Approx(1.5 / 4));
    CHECK(ed.size() == InstrumentEditor::Size{300, 150});
    CHECK(ed.resizable());

    // On attach the test view asks for 320 x 160 (like plugins that adjust on open).
    REQUIRE(ed.attach(nullptr, err));
    REQUIRE(requests.size() == 1);
    CHECK(requests[0] == InstrumentEditor::Size{320, 160});
    CHECK(ed.size() == InstrumentEditor::Size{320, 160});

    // The user resizes the window: the view's limits apply (200 x 100 .. 1200 x 800).
    CHECK(ed.resize({500, 400}) == InstrumentEditor::Size{500, 400});
    CHECK(ed.size() == InstrumentEditor::Size{500, 400});
    CHECK(ed.resize({50, 5000}) == InstrumentEditor::Size{200, 800});
    CHECK(ed.size() == InstrumentEditor::Size{200, 800});
    CHECK(ed.resize({0, 0}) == InstrumentEditor::Size{200, 800});  // minimised: unchanged

    // Unloading the instrument closes its editor; a new one can be opened after loading again.
    inst.unload();
    CHECK_FALSE(ed.isOpen());
    ed.close();  // safe to repeat
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    accept = false;  // a refused request leaves the size as it was
    REQUIRE(ed.create(inst, "minidaw-test", err));
    REQUIRE(ed.attach(nullptr, err));
    CHECK(requests.size() == 2);
    CHECK(ed.size() == InstrumentEditor::Size{300, 150});
    ed.close();
    CHECK_FALSE(ed.isOpen());
    REQUIRE(second.create(inst, "minidaw-test", err));  // free again after close
}

#if defined(_WIN32)
TEST_CASE("T-VST-19: the plugin editor in a Windows window; an edit in it reaches the sound", "[vst][T-VST-19]")
{
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    HWND host = CreateWindowExW(0, L"STATIC", L"editor host", WS_OVERLAPPEDWINDOW, 0, 0, 400, 300, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);  // never shown
    REQUIRE(host != nullptr);
    {
        InstrumentEditor ed;
        REQUIRE(ed.create(inst, "HWND", err));
        REQUIRE(ed.attach(host, err));
        HWND view = GetWindow(host, GW_CHILD);
        REQUIRE(view != nullptr);
        RECT rc;
        GetClientRect(view, &rc);
        CHECK(rc.right - rc.left == 300);
        CHECK(rc.bottom - rc.top == 150);
        CHECK(ed.resize({600, 300}) == InstrumentEditor::Size{600, 300});
        GetClientRect(view, &rc);
        CHECK(rc.right - rc.left == 600);

        // A click on the right half raises the gain from 0.5 to 0.6: in the controller and in the sound.
        SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(500, 100));
        CHECK(inst.controller()->getParamNormalized(1) == Approx(0.6));
        std::vector<float> l(4800), r(4800);
        const InstrumentMidi on = msg(0, 0x90, 69, 127);
        inst.process(&on, 1, 4800, {}, l.data(), r.data());
        CHECK(peak(l, 2400, 4800) == Approx(0.6).margin(0.01));
        ed.close();
        CHECK(GetWindow(host, GW_CHILD) == nullptr);  // the view's window is gone
    }
    DestroyWindow(host);
}
#endif


TEST_CASE("T-VST-21: instrument rack: no hanging notes on disarm, slot shift or a full block; gain per instrument",
          "[vst][T-VST-21]")
{
    std::string err;
    Vst3Instrument a, b;
    REQUIRE(a.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    REQUIRE(b.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    CHECK(a.serial() != b.serial());
    InstrumentRack rack;
    std::vector<float> out(2 * 1024);
    auto run = [&](std::optional<std::uint64_t> timeline, unsigned frames = 512) {
        std::fill(out.begin(), out.end(), 0.0f);
        rack.process(out.data(), frames, 2, timeline);
    };
    auto peakOut = [&](unsigned from, unsigned to) {
        float p = 0;
        for (unsigned f = from; f < to; ++f) p = std::max(p, std::fabs(out[f * 2]));
        return p;
    };

    SECTION("a live note held while the track is disarmed is released")
    {
        InstrumentRack::SlotSetup s;
        s.instrument = &a;
        s.track = 1;
        s.live = true;
        rack.configure({s});
        RawMidi on{0, {0x90, 60, 127}, 3};
        rack.liveRing(0)->write(&on, 1);
        run(std::nullopt);
        CHECK(peakOut(1, 512) == Approx(0.5f).margin(0.01));
        s.live = false;  // disarmed: the key is still down, its note-off never comes
        rack.configure({s});
        run(std::nullopt);
        CHECK(peakOut(1, 512) == 0.0f);
    }

    SECTION("notes and gain follow an instrument to another slot")
    {
        auto take = std::make_shared<std::vector<MidiEvent>>(std::vector<MidiEvent>{{0, {0x90, 69, 127}, 3}});
        InstrumentRack::SlotSetup sa;
        sa.instrument = &a;
        sa.track = 1;
        sa.playback = take;
        InstrumentRack::SlotSetup sb;
        sb.instrument = &b;
        sb.track = 2;
        sb.gain = 0.25f;
        sb.live = true;
        rack.configure({sa, sb});
        run(std::uint64_t{0});  // a plays its take's note
        CHECK(peakOut(1, 512) == Approx(0.5f).margin(0.01));
        rack.configure({sb, sa});  // the slots swap (e.g. a track was moved or removed)
        run(std::nullopt);         // stopped: a's note must be released (it is in slot 1 now)
        CHECK(peakOut(1, 512) == 0.0f);
        // b in slot 0 starts at its own gain 0.25 (no ramp from the gain slot 0 had before)
        RawMidi on{0, {0x90, 60, 127}, 3};
        rack.liveRing(0)->write(&on, 1);
        run(std::nullopt);
        CHECK(peakOut(1, 512) == Approx(0.125f).margin(0.005));
    }

    SECTION("a note-off still arrives when the block's event list is full")
    {
        std::vector<MidiEvent> ev;
        ev.push_back({0, {0x90, 69, 127}, 3});
        for (std::uint64_t f = 1; f <= 600; ++f) ev.push_back({f, {0xB0, 20, 1}, 3});  // controller 20: unmapped
        ev.push_back({700, {0x80, 69, 0}, 3});
        InstrumentRack::SlotSetup s;
        s.instrument = &a;
        s.track = 1;
        s.playback = std::make_shared<std::vector<MidiEvent>>(ev);
        rack.configure({s});
        run(std::uint64_t{0}, 1024);  // 602 events in one block, room for 512
        CHECK(peakOut(1, 690) == Approx(0.5f).margin(0.01));
        CHECK(peakOut(701, 1024) == 0.0f);
    }
    rack.clear();
}

TEST_CASE("T-VST-22: MIDI controllers reach parameters (latest value wins); controller state round-trip",
          "[vst][T-VST-22]")
{
    std::string err;
    Vst3Instrument inst;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    std::vector<float> l(4800), r(4800);

    // Controller 7 is mapped to Gain by the plugin (IMidiMapping). Ten changes
    // in one block (more than the 4 points a queue holds): the processor must
    // end at the last value, 100/127, and the controller (editor) shows it.
    std::vector<InstrumentMidi> ev{msg(0, 0x90, 69, 127)};
    for (std::uint8_t k = 1; k <= 10; ++k) ev.push_back(msg(k, 0xB0, 7, static_cast<std::uint8_t>(10 * k)));
    inst.process(ev.data(), ev.size(), 4800, {}, l.data(), r.data());
    CHECK(peak(l, 2400, 4800) == Approx(100.0 / 127).margin(0.01));
    inst.idle();
    CHECK(inst.controller()->getParamNormalized(1) == Approx(100.0 / 127));

    // Pitch bend and other controllers are not mapped: ignored, no change.
    const InstrumentMidi other[] = {msg(0, 0xB0, 1, 0), msg(1, 0xE0, 0, 0)};
    inst.process(other, 2, 4800, {}, l.data(), r.data());
    CHECK(peak(l, 2400, 4800) == Approx(100.0 / 127).margin(0.01));

    // Controller state: the test synth keeps its editor size there.
    InstrumentEditor ed;
    REQUIRE(ed.create(inst, "minidaw-test", err));
    REQUIRE(ed.attach(nullptr, err));
    CHECK(ed.resize({500, 400}) == InstrumentEditor::Size{500, 400});
    ed.close();
    std::string comp, ctrl;
    REQUIRE(inst.getState(comp, ctrl, err));
    CHECK(ctrl.size() == 8);
    Vst3Instrument again;
    REQUIRE(again.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    REQUIRE(again.setState(comp, ctrl, err));
    InstrumentEditor ed2;
    REQUIRE(ed2.create(again, "minidaw-test", err));
    CHECK(ed2.size() == InstrumentEditor::Size{500, 400});
    CHECK(again.controller()->getParamNormalized(1) == Approx(100.0 / 127));  // the gain, from the component state
}

TEST_CASE("T-VST-23: restart requests from the plugin are reported once", "[vst][T-VST-23]")
{
    std::string err;
    Vst3Instrument inst;
    CHECK_FALSE(inst.takeRestartRequest().any);  // nothing loaded
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    CHECK_FALSE(inst.takeRestartRequest().any);

    inst.controller()->setParamNormalized(4, 1.0);  // test hook: busses and latency changed
    Vst3Instrument::RestartRequest r = inst.takeRestartRequest();
    CHECK(r.any);
    CHECK(r.reload);
    CHECK(r.latencyChanged);
    CHECK_FALSE(inst.takeRestartRequest().any);     // reported once

    inst.controller()->setParamNormalized(4, 0.25); // parameter values changed: nothing to reload
    r = inst.takeRestartRequest();
    CHECK(r.any);
    CHECK_FALSE(r.reload);
    CHECK_FALSE(r.latencyChanged);
    CHECK(inst.latencySamples() == 0);
}

TEST_CASE("T-VST-26: the rack gives each instrument's sound before the track gain, for recording",
          "[vst][T-VST-26]")
{
    std::string err;
    Vst3Instrument a;
    REQUIRE(a.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    InstrumentRack rack;
    InstrumentRack::SlotSetup s;
    s.instrument = &a;
    s.track = 7;
    s.live = true;
    s.gain = 0.0f;  // the track is muted: the output gets nothing, the recording still gets the sound
    rack.configure({s});
    RawMidi on{0, {0x90, 69, 127}, 3};
    rack.liveRing(0)->write(&on, 1);
    std::vector<float> out(2 * 512, 0.0f);
    rack.process(out.data(), 512, 2, std::nullopt);
    float outPeak = 0;
    for (float v : out) outPeak = std::max(outPeak, std::fabs(v));
    CHECK(outPeak == 0.0f);
    const float* l = nullptr;
    const float* r = nullptr;
    REQUIRE(rack.instrumentOutput(7, 512, l, r) == InstrumentSound::Sound);
    float p = 0;
    for (int f = 1; f < 512; ++f) p = std::max(p, std::fabs(l[f]));
    CHECK(p == Approx(0.5f).margin(0.01));
    CHECK(rack.instrumentOutput(8, 512, l, r) == InstrumentSound::None);    // no such track
    CHECK(rack.instrumentOutput(7, 256, l, r) == InstrumentSound::None);    // not this block's length
    std::vector<float> big(2 * 9000, 0.0f);  // longer than the capture (8192 frames): lost, not silence (R7)
    rack.process(big.data(), 9000, 2, std::nullopt);
    CHECK(rack.instrumentOutput(7, 9000, l, r) == InstrumentSound::Lost);
    rack.clear();
    rack.process(out.data(), 512, 2, std::nullopt);
    CHECK(rack.instrumentOutput(7, 512, l, r) == InstrumentSound::None);    // gone with the configuration
}

TEST_CASE("T-VST-29: a MIDI take is rendered through its instrument, with its settings, faster than real time",
          "[vst][T-VST-29]")
{
    test::TempDir dir;
    std::string err;
    // The track's settings: gain 0.25.
    Vst3Instrument live;
    REQUIRE(live.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    live.setParameter(1, 0.25);
    std::vector<float> l(512), r(512);
    live.process(nullptr, 0, 512, {}, l.data(), r.data());
    RenderJob job;
    REQUIRE(live.getState(job.state, job.controllerState, err));
    job.module = MINIDAW_TEST_VST3;
    job.classId = testSynthClass();
    job.sampleRate = 48000;
    job.events = {{5000, {0x80, 69, 0}, 3}, {1000, {0x90, 69, 127}, 3}};  // out of order on purpose
    job.takeFrames = 6000;
    job.out = dir.path() / "render.wav";

    OfflineRenderer render;
    REQUIRE(render.start(job, err));
    CHECK_FALSE(render.start(job, err));  // one at a time
    while (!render.done()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(render.progress() == 1.0);
    const RenderResult res = render.finish();
    INFO(res.error);
    REQUIRE(res.ok);
    CHECK(res.peak == Approx(0.25f).margin(0.01));
    WavData w;
    REQUIRE(readWav(job.out, w, err));
    REQUIRE(w.channels == 2);
    CHECK(w.frames() == res.frames);
    CHECK(w.frames() >= 6000 + 24000 - 512);     // the take plus 0.5 s of silence (the block over the end counts)
    CHECK(w.frames() < 6000 + 24000 + 2 * 512);
    auto peakIn = [&](std::uint64_t a, std::uint64_t b) {
        float p = 0;
        for (std::uint64_t f = a; f < b; ++f) p = std::max(p, std::fabs(w.samples[2 * f]));
        return p;
    };
    CHECK(peakIn(0, 1000) == 0.0f);                          // nothing before the note
    CHECK(peakIn(1001, 5000) == Approx(0.25f).margin(0.01)); // the note, at the saved gain
    CHECK(peakIn(5001, w.frames()) < 1e-4f);                  // released at its note-off

    // Render again (crosscheck #10, R1): a new file each time; cancelling the
    // second leaves the first (which the linked track plays) untouched.
    Track linked;
    linked.number = 2;
    linked.name = "Keys sound";
    linked.kind = TrackKind::Audio;
    const auto first = newRenderPath(dir.path(), linked);
    CHECK(first.filename() == "02-Keys sound-render.wav");
    std::filesystem::copy_file(job.out, first);
    const auto second = newRenderPath(dir.path(), linked);
    CHECK(second.filename() == "02-Keys sound-render-2.wav");
    const auto firstSize = std::filesystem::file_size(first);
    job.out = second;
    job.takeFrames = 48000 * 600;
    REQUIRE(render.start(job, err));
    render.cancel();
    CHECK(render.finish().cancelled);
    CHECK_FALSE(std::filesystem::exists(second));
    REQUIRE(std::filesystem::exists(first));
    CHECK(std::filesystem::file_size(first) == firstSize);
    job.takeFrames = 6000;

    // Cancel: no file is left.
    job.takeFrames = 48000 * 600;  // ten minutes
    job.events = {{0, {0x90, 60, 100}, 3}};
    job.out = dir.path() / "cancelled.wav";
    REQUIRE(render.start(job, err));
    render.cancel();
    const RenderResult c = render.finish();
    CHECK(c.cancelled);
    CHECK_FALSE(c.ok);
    CHECK_FALSE(std::filesystem::exists(job.out));

    // More than the 1024 events a render block takes (R12): held back to the
    // next block, none lost — the last note-off still ends the sound.
    job.out = dir.path() / "many.wav";
    job.events.clear();
    for (std::uint64_t k = 0; k < 1500; ++k) job.events.push_back({100, {0xB0, 20, 1}, 3});  // unmapped controller
    job.events.push_back({100, {0x90, 69, 127}, 3});
    job.events.push_back({2000, {0x80, 69, 0}, 3});
    job.takeFrames = 2500;
    REQUIRE(render.start(job, err));
    while (!render.done()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(render.finish().ok);
    REQUIRE(readWav(job.out, w, err));
    CHECK(peakIn(512, 1024) > 0.2f);           // the held-back note played in the next block
    CHECK(peakIn(2048, w.frames()) < 1e-4f);   // and its note-off ends it (no hanging note)

    // A note that never ends: the tail stops at maxTailSeconds (R12).
    job.out = dir.path() / "tail.wav";
    job.events = {{0, {0x90, 69, 127}, 3}};
    job.takeFrames = 4800;
    job.maxTailSeconds = 1.0;
    REQUIRE(render.start(job, err));
    while (!render.done()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const RenderResult tail = render.finish();
    REQUIRE(tail.ok);
    CHECK(tail.frames >= 4800 + 48000);
    CHECK(tail.frames < 4800 + 48000 + 2 * 512);
    job.maxTailSeconds = 10.0;

    // A module that cannot be loaded: refused at start.
    job.module = dir.path() / "missing.vst3";
    CHECK_FALSE(render.start(job, err));
    CHECK_FALSE(err.empty());
}


TEST_CASE("T-VST-33: the plugin's program list: names, current, choosing one changes the sound", "[vst][T-VST-33]")
{
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    CHECK(inst.vendor() == "mini-daw tests");
    Vst3Instrument::Programs p;
    REQUIRE(inst.programs(p));
    CHECK(p.listName == "Factory");
    REQUIRE(p.names == std::vector<std::string>{"Soft", "Medium", "Loud"});
    CHECK(inst.currentProgram() == 1);  // Medium

    std::vector<float> l(1024), r(1024);
    const InstrumentMidi on = msg(0, 0x90, 69, 127);
    REQUIRE(inst.selectProgram(2, err));  // Loud: gain 0.9
    CHECK(inst.currentProgram() == 2);
    inst.process(&on, 1, 1024, {}, l.data(), r.data());
    CHECK(peak(l, 0, 1024) == Approx(0.9f).margin(0.01));
    REQUIRE(inst.selectProgram(0, err));  // Soft: gain 0.2
    inst.process(nullptr, 0, 1024, {}, l.data(), r.data());
    CHECK(peak(l, 0, 1024) == Approx(0.2f).margin(0.01));
    CHECK_FALSE(inst.selectProgram(3, err));
    CHECK_FALSE(inst.selectProgram(-1, err));
}

TEST_CASE("T-VST-34: a preset saved from an instrument loads its sound back; another plugin's is refused",
          "[vst][T-VST-34]")
{
    test::TempDir tmp;
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    inst.setParameter(1, 0.75);
    std::vector<float> l(1024), r(1024);
    const InstrumentMidi on = msg(0, 0x90, 69, 127);
    inst.process(&on, 1, 1024, {}, l.data(), r.data());  // gain 0.75 reaches the processor
    const fs::path file = tmp.path() / "Wärm pad.vstpreset";
    REQUIRE(inst.savePreset(file, err));
    INFO(err);

    inst.setParameter(1, 0.1);
    inst.process(nullptr, 0, 1024, {}, l.data(), r.data());
    CHECK(peak(l, 0, 1024) == Approx(0.1f).margin(0.01));
    REQUIRE(inst.loadPreset(file, err));
    inst.process(nullptr, 0, 1024, {}, l.data(), r.data());
    CHECK(peak(l, 0, 1024) == Approx(0.75f).margin(0.01));

    // The class ID in the file is the FUID text other hosts write and read.
    std::string bytes;
    REQUIRE(readWholeFile(file, bytes, err));
    auto d = decodeVstPreset(bytes, err);
    REQUIRE(d);
    d->classId = "00000000000000000000000000000001";
    const fs::path other = tmp.path() / "other.vstpreset";
    REQUIRE(writeFileReplacing(other, encodeVstPreset(*d), err));
    CHECK_FALSE(inst.loadPreset(other, err));
    CHECK(err.find("another plugin") != std::string::npos);
}
