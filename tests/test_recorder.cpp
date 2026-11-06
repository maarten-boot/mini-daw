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
    bool lose = false;  // the next block is "too long": lost
    InstrumentSound instrumentOutput(int track, unsigned int frames, const float*& left,
                                     const float*& right) const override
    {
        if (track != 5) return InstrumentSound::None;
        if (lose) return InstrumentSound::Lost;
        l.resize(frames);
        r.resize(frames);
        for (unsigned int f = 0; f < frames; ++f) {
            l[f] = marker(at + f, 10);
            r[f] = marker(at + f, 11);
        }
        left = l.data();
        right = r.data();
        return InstrumentSound::Sound;
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
        inst.lose = true;  // a block whose sound could not be kept: counted as lost, silence in its place (R7)
        rec.process(nullptr, block, 0, 4 * block, &inst);
        const RecordingResult res = rec.stop();
        CHECK(res.takes.at(0).frames == 5 * block);
        CHECK(res.takes.at(0).droppedFrames == block);
        CHECK(res.lostData());
        Recorder needsInput;
        CHECK_FALSE(needsInput.start({{dir / "in.wav", {0}}}, 48000, 0, err));  // an input take still needs input
    }
}

TEST_CASE("T-REC-34: parts mode: a part starts in the past, ends exactly, nothing between parts",
          "[recorder][T-REC-34]")
{
    test::TempDir dir;
    constexpr unsigned int rate = 1000;  // 1 frame = 1 ms, easy numbers
    constexpr unsigned int block = 100;
    Recorder rec;
    std::string err;
    RecorderOptions opt;
    opt.parts = true;
    opt.partBufferSeconds = 2.0;  // 2000 frames remembered
    opt.startDiskThread = false;
    opt.syncToDisk = false;
    REQUIRE(rec.start({{dir / "unused.wav", {1}}}, rate, 2, err, opt));
    std::uint64_t fed = 0;  // recorder frames fed so far
    auto feed = [&](unsigned int blocks) {
        std::vector<float> in(block * 2);
        for (unsigned int b = 0; b < blocks; ++b) {
            for (unsigned int f = 0; f < block; ++f) {
                in[f * 2] = 0.0f;
                in[f * 2 + 1] = marker(fed + f, 0);
            }
            rec.process(in.data(), block, 2, 5000 + fed);
            fed += block;
        }
    };
    auto check = [&](const std::filesystem::path& p, std::uint64_t from, std::uint64_t frames) {
        WavData w;
        REQUIRE(readWav(p, w, err));
        REQUIRE(w.frames() == frames);
        bool same = true;
        for (std::uint64_t i = 0; i < frames; ++i) same = same && toStep(w.samples[i]) == toStep(marker(from + i, 0));
        CHECK(same);
    };

    feed(30);  // 0 .. 2999: waiting, nothing written
    rec.drainForTest();
    CHECK_FALSE(std::filesystem::exists(dir / "unused.wav"));
    CHECK(rec.startFrame() == std::optional<std::uint64_t>(5000));

    // Part 1 begins 500 frames in the past; it ends at 3500 while the disk is behind.
    rec.beginPart(1, 2500, {dir / "p1.wav"});
    rec.drainForTest();
    feed(10);  // .. 3999
    rec.endPart(3500);
    rec.drainForTest();
    auto done = rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK(done[0].part == 1);
    CHECK(done[0].frames == 1000);
    CHECK(done[0].error.empty());
    check(dir / "p1.wav", 2500, 1000);

    // Part 2: the disk already wrote past its end (4600): the rest is cut off.
    rec.beginPart(2, 4200, {dir / "p2.wav"});
    feed(10);  // .. 4999
    rec.drainForTest();
    rec.endPart(4600);
    rec.drainForTest();
    done = rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK(done[0].frames == 400);
    check(dir / "p2.wav", 4200, 400);

    // Part 3 asks for more than is remembered: it starts 2000 frames back.
    feed(20);  // .. 6999
    rec.drainForTest();
    rec.beginPart(3, 0, {dir / "p3.wav"});
    feed(5);  // .. 7499
    // Stop with the part open: it ends with the last frame.
    const RecordingResult res = rec.stop();
    CHECK(res.takes.empty());
    CHECK(res.error.empty());
    done = rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK(done[0].part == 3);
    check(dir / "p3.wav", 5000, 2500);
}

TEST_CASE("T-REC-34: parts mode notes when a take was loud", "[recorder][T-REC-34]")
{
    test::TempDir dir;
    Recorder rec;
    std::string err;
    RecorderOptions opt;
    opt.parts = true;
    opt.startDiskThread = false;
    opt.activityLevel = 0.01f;  // -40 dBFS
    REQUIRE(rec.start({{dir / "x.wav", {0}}}, 48000, 1, err, opt));
    std::vector<float> quiet(256, 0.009f);
    std::vector<float> loud(256, 0.009f);
    loud[100] = -0.02f;
    rec.process(quiet.data(), 256, 1, 1000);
    CHECK(rec.lastActiveFrame() == 0);
    rec.process(loud.data(), 256, 1, 1256);
    rec.process(quiet.data(), 256, 1, 1512);
    CHECK(rec.lastActiveFrame() == 1512);  // the end of the loud block
    rec.stop();
}

namespace {
struct PartsRig {
    test::TempDir dir;
    Recorder rec;
    std::uint64_t fed = 0;
    std::string err;
    explicit PartsRig(double ringSeconds = 4.0)
    {
        RecorderOptions opt;
        opt.parts = true;
        opt.partBufferSeconds = 2.0;
        opt.ringSeconds = ringSeconds;
        opt.startDiskThread = false;
        opt.syncToDisk = false;
        REQUIRE(rec.start({{dir / "unused.wav", {1}}}, 1000, 2, err, opt));
    }
    void feed(unsigned int blocks)
    {
        std::vector<float> in(200);
        for (unsigned int b = 0; b < blocks; ++b) {
            for (unsigned int f = 0; f < 100; ++f) in[f * 2 + 1] = marker(fed + f, 0);
            rec.process(in.data(), 100, 2, fed);
            fed += 100;
        }
    }
};
} // namespace

TEST_CASE("T-REC-34: parts mode: audio lost before and inside a part is counted, cut-off loss is not (S5)",
          "[recorder][T-REC-34]")
{
    PartsRig r(0.25);  // the ring holds 250 frames: the third block without a drain is lost
    r.feed(3);         // 0..299: 200..299 lost
    r.rec.drainForTest();
    r.feed(1);  // 300..399 (announces the loss)
    r.rec.drainForTest();
    r.rec.beginPart(1, 100, {r.dir / "p1.wav"});  // 100..: the loss is in the remembered part
    r.feed(1);
    r.rec.endPart(450);
    r.rec.drainForTest();
    auto done = r.rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK(done[0].frames == 350);
    CHECK(done[0].lostFrames == 100);

    // Part 2 ends at 650; a loss at 700..799 was already in the file: cut off, not counted.
    r.rec.beginPart(2, 500, {r.dir / "p2.wav"});
    r.feed(3);  // 500..799: 700..799 lost
    r.rec.drainForTest();
    r.feed(1);  // 800..899
    r.rec.drainForTest();
    r.rec.endPart(650);
    r.rec.drainForTest();
    done = r.rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK(done[0].frames == 150);
    CHECK(done[0].lostFrames == 0);
    r.rec.stop();
}

TEST_CASE("T-REC-34: parts mode: a part file that cannot be made; stop with a part still queued",
          "[recorder][T-REC-34]")
{
    PartsRig r;
    r.feed(10);
    r.rec.drainForTest();
    r.rec.beginPart(1, 500, {r.dir / "no-such-folder" / "p1.wav"});
    r.rec.drainForTest();
    r.feed(2);
    r.rec.endPart(1100);
    r.rec.drainForTest();
    auto done = r.rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK_FALSE(done[0].error.empty());
    CHECK(done[0].frames == 0);
    CHECK_FALSE(r.rec.diskError().empty());

    // A part asked for just before Stop is still written, from memory.
    r.rec.beginPart(2, 1000, {r.dir / "p2.wav"});
    r.rec.stop();
    done = r.rec.takeFinishedParts();
    REQUIRE(done.size() == 1);
    CHECK(done[0].frames == 200);
    WavData w;
    REQUIRE(readWav(r.dir / "p2.wav", w, r.err));
    CHECK(toStep(w.samples.at(0)) == toStep(marker(1000, 0)));
}

TEST_CASE("T-REC-34: parts mode with the disk thread running", "[recorder][T-REC-34]")
{
    test::TempDir dir;
    constexpr unsigned int rate = 48000;
    constexpr unsigned int block = 480;
    Recorder rec;
    std::string err;
    RecorderOptions opt;
    opt.parts = true;
    opt.syncToDisk = false;
    opt.headerUpdateSeconds = 0.05;
    REQUIRE(rec.start({{dir / "unused.wav", {0}}}, rate, 1, err, opt));
    std::vector<float> in(block);
    std::uint64_t fed = 0;
    auto feed = [&](int blocks) {
        for (int b = 0; b < blocks; ++b) {
            for (unsigned int f = 0; f < block; ++f) in[f] = marker(fed + f, 0);
            rec.process(in.data(), block, 1, fed);
            fed += block;
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    };
    feed(100);                                  // 0 .. 48000
    rec.beginPart(1, 30000, {dir / "a.wav"});   // from memory
    feed(100);                                  // .. 96000
    rec.endPart(70000);
    feed(50);
    rec.beginPart(2, 110000, {dir / "b.wav"});
    feed(20);
    rec.stop();  // part 2 open: ends with the last frame (129600)
    const auto done = rec.takeFinishedParts();
    REQUIRE(done.size() == 2);
    WavData a;
    REQUIRE(readWav(dir / "a.wav", a, err));
    REQUIRE(a.frames() == 40000);
    WavData b;
    REQUIRE(readWav(dir / "b.wav", b, err));
    REQUIRE(b.frames() == 19600);
    bool same = true;
    for (std::uint64_t i = 0; i < 40000; ++i) same = same && toStep(a.samples[i]) == toStep(marker(30000 + i, 0));
    for (std::uint64_t i = 0; i < 19600; ++i) same = same && toStep(b.samples[i]) == toStep(marker(110000 + i, 0));
    CHECK(same);
}
