// Recording engine (SPEC-REC-02, 03, 05, SPEC-NF-01): T-REC-08, T-REC-09, T-REC-13, T-REC-14.
// A test thread plays the audio callback; the real disk thread writes WAV files.

#include "minidaw/recorder.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace minidaw;

namespace {

// Sample value for (frame, channel) that survives the 24-bit round trip exactly.
float marker(std::uint64_t frame, unsigned int channel)
{
    const auto step = static_cast<std::int32_t>((frame * 7 + channel * 1000003) % 8000000) - 4000000;
    return static_cast<float>(static_cast<double>(step) / 8388607.0);
}

std::int32_t toStep(float v)
{
    return static_cast<std::int32_t>(std::lround(static_cast<double>(v) * 8388607.0));
}

} // namespace

TEST_CASE("T-REC-08: takes record the chosen channels, sample-exact, in parallel", "[recorder][T-REC-08]")
{
    test::TempDir dir;
    constexpr unsigned int rate = 48000;
    constexpr unsigned int inCh = 4;
    constexpr unsigned int block = 256;
    constexpr std::uint64_t blocks = 5 * rate / block;  // about 5 s of audio, fed as fast as allowed

    Recorder rec;
    std::string err;
    std::vector<TakeSpec> takes = {{dir / "mono.wav", {2}}, {dir / "stereo.wav", {3, 0}}};
    RecorderOptions opt;
    opt.headerUpdateSeconds = 0.05;
    REQUIRE(rec.start(takes, rate, inCh, err, opt));
    CHECK(rec.recording());

    // Feed blocks a little faster than real time (2 ms per 5.3 ms block).
    std::vector<float> in(block * inCh);
    for (std::uint64_t b = 0; b < blocks; ++b) {
        for (unsigned int f = 0; f < block; ++f)
            for (unsigned int c = 0; c < inCh; ++c) in[f * inCh + c] = marker(b * block + f, c);
        rec.process(in.data(), block, inCh);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(rec.framesCaptured() == blocks * block);
    const RecordingResult res = rec.stop();
    CHECK_FALSE(rec.recording());

    REQUIRE(res.error.empty());
    REQUIRE(res.takes.size() == 2);
    CHECK_FALSE(res.lostData());
    for (const auto& t : res.takes) {
        CHECK(t.frames == blocks * block);
        CHECK(t.droppedFrames == 0);
    }

    WavData mono, stereo;
    REQUIRE(readWav(dir / "mono.wav", mono, err));
    REQUIRE(readWav(dir / "stereo.wav", stereo, err));
    REQUIRE(mono.channels == 1);
    REQUIRE(stereo.channels == 2);
    REQUIRE(mono.frames() == blocks * block);
    REQUIRE(stereo.frames() == blocks * block);
    std::uint64_t mismatches = 0;
    for (std::uint64_t f = 0; f < mono.frames(); ++f) {
        if (toStep(mono.samples[f]) != toStep(marker(f, 2))) ++mismatches;
        if (toStep(stereo.samples[2 * f]) != toStep(marker(f, 3))) ++mismatches;
        if (toStep(stereo.samples[2 * f + 1]) != toStep(marker(f, 0))) ++mismatches;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("T-REC-09: when the disk cannot keep up, lost blocks are counted", "[recorder][T-REC-09]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    RecorderOptions opt;
    opt.ringSeconds = 4.0;          // 4096 frames at 1024 Hz (the ring rounds up to a power of two)
    opt.startDiskThread = false;    // nobody empties the ring
    REQUIRE(rec.start({{dir / "a.wav", {0}}}, 1024, 1, err, opt));

    std::vector<float> in(1000, 0.1f);
    for (int i = 0; i < 10; ++i) rec.process(in.data(), 1000, 1);  // 10000 frames, only 4 blocks fit
    CHECK(rec.droppedFrames() == 6000);

    const RecordingResult res = rec.stop();  // stop() still writes what was buffered
    REQUIRE(res.takes.size() == 1);
    CHECK(res.takes[0].frames == 10000);     // the lost part is silence: the take keeps its length
    CHECK(res.takes[0].droppedFrames == 6000);
    CHECK(res.lostData());

    WavData d;
    REQUIRE(readWav(dir / "a.wav", d, err));
    REQUIRE(d.frames() == 10000);
    CHECK(std::abs(d.samples[3999] - 0.1f) < 1e-6f);
    CHECK(d.samples[4000] == 0.0f);
    CHECK(d.samples[9999] == 0.0f);
}

TEST_CASE("T-REC-13: a lost block becomes silence, so later audio stays in place", "[recorder][T-REC-13]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    RecorderOptions opt;
    opt.ringSeconds = 4.0;          // 4096 frames at 1024 Hz
    opt.startDiskThread = false;
    REQUIRE(rec.start({{dir / "mono.wav", {0}}, {dir / "stereo.wav", {1, 0}}}, 1024, 2, err, opt));

    std::vector<float> in(2 * 1000);
    std::uint64_t frame = 0;
    auto feed = [&](int blocks) {
        for (int b = 0; b < blocks; ++b) {
            for (unsigned int f = 0; f < 1000; ++f) {
                in[2 * f] = marker(frame + f, 0);
                in[2 * f + 1] = marker(frame + f, 1);
            }
            rec.process(in.data(), 1000, 2);
            frame += 1000;
        }
    };
    feed(5);              // frames 0..3999 fit, 4000..4999 are lost
    rec.drainForTest();   // the disk catches up
    feed(2);              // frames 5000..6999 must stay at their place
    const RecordingResult res = rec.stop();

    REQUIRE(res.takes.size() == 2);
    for (const auto& t : res.takes) {
        CHECK(t.frames == 7000);
        CHECK(t.droppedFrames == 1000);
    }
    WavData mono, stereo;
    REQUIRE(readWav(dir / "mono.wav", mono, err));
    REQUIRE(readWav(dir / "stereo.wav", stereo, err));
    REQUIRE(mono.frames() == 7000);
    REQUIRE(stereo.frames() == 7000);
    std::size_t bad = 0;
    for (std::uint64_t f = 0; f < 7000; ++f) {
        const bool lost = f >= 4000 && f < 5000;
        const std::int32_t want0 = lost ? 0 : toStep(marker(f, 0));
        const std::int32_t want1 = lost ? 0 : toStep(marker(f, 1));
        if (toStep(mono.samples[f]) != want0) ++bad;
        if (toStep(stereo.samples[2 * f]) != want1) ++bad;
        if (toStep(stereo.samples[2 * f + 1]) != want0) ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("T-REC-14: frames lost on a disk error are counted", "[recorder][T-REC-14]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    RecorderOptions opt;
    opt.startDiskThread = false;
    REQUIRE(rec.start({{dir / "full.wav", {0}}}, 1024, 1, err, opt));
    std::vector<float> in(1000, 0.1f);
    rec.process(in.data(), 1000, 1);
    rec.drainForTest();                // 1000 frames reach the file
    rec.failNextDiskWriteForTest();    // then the disk is "full"
    for (int i = 0; i < 3; ++i) rec.process(in.data(), 1000, 1);
    rec.drainForTest();
    CHECK(rec.droppedFrames() == 3000);  // visible while recording
    CHECK_FALSE(rec.diskError().empty());
    rec.process(in.data(), 1000, 1);     // after the error nothing is written
    const RecordingResult res = rec.stop();
    REQUIRE(res.takes.size() == 1);
    CHECK_FALSE(res.error.empty());
    CHECK(res.takes[0].frames == 1000);
    CHECK(res.takes[0].droppedFrames == 4000);
    CHECK(res.lostData());
    WavData d;
    REQUIRE(readWav(dir / "full.wav", d, err));  // valid up to the error
    CHECK(d.frames() == 1000);
}

TEST_CASE("Recorder refuses bad takes without creating files", "[recorder]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    CHECK_FALSE(rec.start({}, 48000, 2, err));
    CHECK_FALSE(rec.start({{dir / "a.wav", {0}}, {dir / "b.wav", {5}}}, 48000, 2, err));
    CHECK(err.find("Input channel 6") != std::string::npos);
    CHECK_FALSE(rec.start({{dir / "c.wav", {0, 1, 1}}}, 48000, 2, err));
    CHECK_FALSE(rec.start({{dir / "d.wav", {0}}}, 48000, 0, err));
    CHECK(std::filesystem::is_empty(dir.path()));

    // An unwritable path for the second take removes the first file again.
    CHECK_FALSE(rec.start({{dir / "e.wav", {0}}, {dir / "missing" / "f.wav", {1}}}, 48000, 2, err));
    CHECK(std::filesystem::is_empty(dir.path()));
    CHECK_FALSE(rec.recording());
}

TEST_CASE("Blocks with a different channel count are ignored", "[recorder]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    REQUIRE(rec.start({{dir / "a.wav", {0}}}, 48000, 2, err));
    std::vector<float> in(300, 0.0f);
    rec.process(in.data(), 100, 3);  // wrong layout: skipped
    rec.process(in.data(), 100, 2);
    rec.process(nullptr, 100, 2);
    const auto res = rec.stop();
    CHECK(res.takes[0].frames == 100);
    CHECK_FALSE(res.lostData());
}

TEST_CASE("process() after stop() does nothing", "[recorder]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    REQUIRE(rec.start({{dir / "a.wav", {0}}}, 48000, 1, err));
    rec.stop();
    std::vector<float> in(64, 0.0f);
    rec.process(in.data(), 64, 1);  // must not crash or write
    CHECK(rec.framesCaptured() == 0);
}

namespace {
// The rack stand-in: track 5's instrument makes frame-numbered values, track 6 has none.
class FakeInstruments : public InstrumentSource {
public:
    std::uint64_t at = 0;  // stream frame of the block
    mutable std::vector<float> l, r;
    bool instrumentOutput(int track, unsigned int frames, const float*& left, const float*& right) const override
    {
        if (track != 5) return false;
        l.resize(frames);
        r.resize(frames);
        for (unsigned int f = 0; f < frames; ++f) {
            l[f] = marker(at + f, 10);
            r[f] = marker(at + f, 11);
        }
        left = l.data();
        right = r.data();
        return true;
    }
};
} // namespace

TEST_CASE("T-VST-25: instrument takes record the instrument's stereo sound in step with the inputs",
          "[recorder][T-VST-25]")
{
    test::TempDir dir;
    FakeInstruments inst;
    std::string err;
    constexpr unsigned int block = 128;

    SECTION("with an input take")
    {
        Recorder rec;
        TakeSpec in{dir / "in.wav", {1}};
        TakeSpec synth{dir / "synth.wav", {}, 5};
        TakeSpec none{dir / "none.wav", {}, 6};
        REQUIRE(rec.start({in, synth, none}, 48000, 2, err));
        std::vector<float> buf(block * 2);
        for (std::uint64_t b = 0; b < 20; ++b) {
            for (unsigned int f = 0; f < block; ++f) buf[f * 2 + 1] = marker(b * block + f, 1);
            inst.at = b * block;
            rec.process(buf.data(), block, 2, b * block, &inst);
        }
        rec.process(nullptr, block, 2, 0, &inst);  // no input: skipped for all takes, they stay in step
        const RecordingResult res = rec.stop();
        REQUIRE(res.error.empty());
        WavData a, s, n;
        REQUIRE(readWav(dir / "in.wav", a, err));
        REQUIRE(readWav(dir / "synth.wav", s, err));
        REQUIRE(readWav(dir / "none.wav", n, err));
        CHECK(a.frames() == 20 * block);
        REQUIRE(s.channels == 2);
        REQUIRE(s.frames() == 20 * block);
        CHECK(n.frames() == 20 * block);
        std::uint64_t bad = 0;
        for (std::uint64_t f = 0; f < s.frames(); ++f) {
            if (toStep(a.samples[f]) != toStep(marker(f, 1))) ++bad;
            if (toStep(s.samples[2 * f]) != toStep(marker(f, 10))) ++bad;
            if (toStep(s.samples[2 * f + 1]) != toStep(marker(f, 11))) ++bad;
            if (n.samples[2 * f] != 0.0f) ++bad;  // no instrument: silence
        }
        CHECK(bad == 0);
    }

    SECTION("without an input device")
    {
        Recorder rec;
        REQUIRE(rec.start({{dir / "synth.wav", {}, 5}}, 48000, 0, err));
        for (std::uint64_t b = 0; b < 4; ++b) {
            inst.at = b * block;
            rec.process(nullptr, block, 0, b * block, &inst);
        }
        CHECK(rec.stop().takes.at(0).frames == 4 * block);
        Recorder needsInput;
        CHECK_FALSE(needsInput.start({{dir / "in.wav", {0}}}, 48000, 0, err));  // an input take still needs input
    }
}
