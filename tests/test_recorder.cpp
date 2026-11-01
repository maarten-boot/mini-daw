// Recording engine (SPEC-REC-03, SPEC-REC-05, SPEC-NF-01): T-REC-08, T-REC-09.
// A test thread plays the audio callback; the real disk thread writes WAV files.

#include "minidaw/recorder.hpp"
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
    CHECK(res.takes[0].frames == 4000);
    CHECK(res.takes[0].droppedFrames == 6000);
    CHECK(res.lostData());
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
