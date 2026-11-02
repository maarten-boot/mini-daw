// Playback: WAV reader, MIDI file timeline, latency estimate, player engine
// (SPEC-PLAY-02, 03, 04, 05).

#include "minidaw/midi_player.hpp"
#include "minidaw/player.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/wav.hpp"
#include "minidaw/wav_reader.hpp"
#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace minidaw;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// A take whose sample n (channel c) is value(n, c): easy to check positions.
float value(std::uint64_t n, unsigned int c)
{
    return static_cast<float>((n % 1000) + 1) / 2048.0f * (c == 0 ? 1.0f : -1.0f);
}

fs::path makeTake(const fs::path& dir, const std::string& name, std::uint64_t frames, unsigned int channels,
                  unsigned int rate = 48000)
{
    const fs::path p = dir / name;
    WavWriter w;
    std::string err;
    REQUIRE(w.open(p, rate, channels, err));
    std::vector<float> buf(static_cast<std::size_t>(frames) * channels);
    for (std::uint64_t n = 0; n < frames; ++n) {
        for (unsigned int c = 0; c < channels; ++c) buf[n * channels + c] = value(n, c);
    }
    REQUIRE(w.write(buf.data(), frames, err));
    REQUIRE(w.close(err));
    return p;
}

// Play `blocks` blocks of `frames`, filling the rings before each (no thread).
std::vector<float> play(Player& p, unsigned int frames, unsigned int blocks, std::uint64_t firstStreamFrame = 0)
{
    std::vector<float> all;
    for (unsigned int b = 0; b < blocks; ++b) {
        p.fillForTest();
        std::vector<float> out(frames * 2, 0.0f);
        p.process(out.data(), frames, 2, firstStreamFrame + static_cast<std::uint64_t>(b) * frames);
        all.insert(all.end(), out.begin(), out.end());
    }
    return all;
}

} // namespace

TEST_CASE("T-PLAY-05: WAV reader: formats, seek, damaged and crashed files", "[play][T-PLAY-05]")
{
    test::TempDir dir;
    const fs::path p = makeTake(dir.path(), "a.wav", 5000, 2);
    WavReader r;
    std::string err;
    REQUIRE(r.open(p, err));
    CHECK(r.sampleRate() == 48000);
    CHECK(r.channels() == 2);
    CHECK(r.frames() == 5000);
    std::vector<float> buf(200);
    REQUIRE(r.seek(4950));
    CHECK(r.read(buf.data(), 100) == 50);  // only 50 left
    CHECK(buf[0] == Approx(value(4950, 0)).margin(1e-6));
    CHECK(buf[1] == Approx(value(4950, 1)).margin(1e-6));
    CHECK(r.read(buf.data(), 10) == 0);

    // A take whose header was never refreshed (crash): data size 0 -> to the end of the file.
    {
        std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(40);
        const char zero[4] = {0, 0, 0, 0};
        f.write(zero, 4);
    }
    REQUIRE(r.open(p, err));
    CHECK(r.frames() == 5000);

    // 16-bit and 32-bit float files written by other programs.
    for (int kind = 0; kind < 2; ++kind) {
        const fs::path q = dir.path() / (kind == 0 ? "16.wav" : "f32.wav");
        const std::uint16_t tag = kind == 0 ? 1 : 3, bits = kind == 0 ? 16 : 32;
        std::ofstream o(q, std::ios::binary);
        auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) o.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
        auto put16 = [&](std::uint16_t v) { o.put(static_cast<char>(v & 0xFF)); o.put(static_cast<char>(v >> 8)); };
        const std::uint32_t frames = 3, block = bits / 8;
        o.write("RIFF", 4); put32(36 + frames * block); o.write("WAVE", 4);
        o.write("LIST", 4); put32(4); o.write("INFO", 4);   // an extra chunk before fmt
        o.write("fmt ", 4); put32(16); put16(tag); put16(1); put32(44100); put32(44100 * block); put16(static_cast<std::uint16_t>(block)); put16(bits);
        o.write("data", 4); put32(frames * block);
        if (kind == 0) { put16(16384); put16(static_cast<std::uint16_t>(-16384)); put16(0); }
        else { const float v[3] = {0.5f, -0.25f, 0.0f}; o.write(reinterpret_cast<const char*>(v), 12); }
        o.close();
        REQUIRE(r.open(q, err));
        CHECK(r.sampleRate() == 44100);
        float s[3] = {};
        CHECK(r.read(s, 3) == 3);
        CHECK(s[0] == Approx(0.5f));
        CHECK(s[1] == Approx(kind == 0 ? -0.5f : -0.25f));
    }

    std::ofstream(dir.path() / "junk.wav") << "not a wav file at all";
    CHECK_FALSE(r.open(dir.path() / "junk.wav", err));
    CHECK(err.find("not a WAV") != std::string::npos);
    CHECK_FALSE(r.open(dir.path() / "missing.wav", err));
}

TEST_CASE("T-PLAY-06: MIDI file to timeline frames, with tempo changes", "[play][T-PLAY-06]")
{
    test::TempDir dir;
    // Our own file: frames survive the round trip (within one tick).
    SmfTrack t;
    t.events = {{0, {0x90, 60, 100}, 3}, {24000, {0x80, 60, 0}, 3}, {48000, {0xB0, 64, 127}, 3}};
    std::string err;
    for (MidiTiming timing : {MidiTiming::Ppq, MidiTiming::Smpte}) {
        REQUIRE(writeSmf(dir.path() / "a.mid", "x", {t}, 48000, timing, 96000, false, err));
        SmfData d;
        REQUIRE(readSmf(dir.path() / "a.mid", d, err));
        std::uint64_t end = 0;
        const auto ev = smfToFrames(d, 48000, &end);
        REQUIRE(ev.size() == 3);
        CHECK(ev[1].frame == 24000);
        CHECK(ev[2].frame == 48000);
        CHECK(ev[2].data[0] == 0xB0);
        CHECK(end == 96000);
        // At 44.1 kHz the same times give other frames.
        CHECK(smfToFrames(d, 44100)[1].frame == 22050);
    }

    // A file from another program: tempo 60 BPM from tick 960 on (PPQ 480 at first 120 BPM).
    SmfData d;
    d.division = 480;
    d.tracks.resize(2);
    d.tracks[0].push_back({960, true, 0x51, {0x0F, 0x42, 0x40}});  // 1 000 000 us per quarter
    d.tracks[1].push_back({480, false, 0, {0x90, 60, 100}});        // 0.5 s
    d.tracks[1].push_back({1440, false, 0, {0x80, 60, 0}});         // 1 s + 1 quarter at 60 BPM = 2 s
    const auto ev = smfToFrames(d, 1000);
    REQUIRE(ev.size() == 2);
    CHECK(ev[0].frame == 500);
    CHECK(ev[1].frame == 2000);
}

TEST_CASE("T-PLAY-07: latency estimate for MIDI playback and overdub", "[play][T-PLAY-07]")
{
    const auto e = estimateLatency(256, 1000, 0.0, 48000);
    CHECK(e.midiFrames == 2 * 256 + 500);
    CHECK(e.audioFrames == 2 * 256 + 1000);
    CHECK(estimateLatency(256, 0, 10.0, 48000).audioFrames == 512 + 480);  // +10 ms correction
    CHECK(estimateLatency(256, 0, -5.0, 48000).audioFrames == 512 - 240);
}

TEST_CASE("T-PLAY-08: player: clips in sync on the timeline, gains, end", "[play][T-PLAY-08]")
{
    test::TempDir dir;
    const fs::path mono = makeTake(dir.path(), "m.wav", 3000, 1);
    const fs::path stereo = makeTake(dir.path(), "s.wav", 2000, 2);
    Player p;
    std::vector<std::string> warnings;

    SECTION("a clip starting later is silent until its start; mono plays on both sides")
    {
        p.setGain(1, 1.0f, 1.0f);
        p.setGain(2, 0.0f, 0.0f);  // muted
        p.startForTest({{1, mono, 1000}, {2, stereo, 0}}, 0, 48000, warnings);
        CHECK(warnings.empty());
        CHECK(p.length() == 4000);
        const auto out = play(p, 500, 4);  // timeline 0..1999
        CHECK(out[999 * 2] == 0.0f);               // before the clip
        CHECK(out[1000 * 2] == Approx(value(0, 0)).margin(1e-6));
        CHECK(out[1000 * 2 + 1] == Approx(value(0, 0)).margin(1e-6));  // mono on the right too
        CHECK(out[1999 * 2] == Approx(value(999, 0)).margin(1e-6));
        CHECK(p.position() == 2000);
        CHECK_FALSE(p.finished());
        play(p, 1000, 2);
        CHECK(p.finished());
        p.stop();
        CHECK_FALSE(p.playing());
        CHECK(p.position() == 4000);  // stays where it stopped
    }
    SECTION("start in the middle; stereo keeps left and right; gains apply")
    {
        p.setGain(2, 0.5f, 1.0f);
        p.startForTest({{2, stereo, 0}}, 700, 48000, warnings);
        // The first block ramps from the start gain, which is already 0.5/1.0.
        const auto out = play(p, 100, 1);
        CHECK(out[0] == Approx(0.5f * value(700, 0)).margin(1e-6));
        CHECK(out[1] == Approx(value(700, 1)).margin(1e-6));
    }
    SECTION("a clip with a negative start (overdub shift) is played from the right frame")
    {
        p.startForTest({{1, mono, -250}}, 0, 48000, warnings);
        const auto out = play(p, 100, 1);
        CHECK(out[0] == Approx(value(250, 0)).margin(1e-6));
    }
    SECTION("anchor maps stream frames to the timeline")
    {
        p.startForTest({{1, mono, 0}}, 5000, 48000, warnings);
        CHECK_FALSE(p.anchor().has_value());
        play(p, 256, 2, 100000);
        REQUIRE(p.anchor().has_value());
        CHECK(p.anchor()->first == 100000);
        CHECK(p.timelineAt(100256) == 5256);
        CHECK(p.timelineAt(99000) == 4000);
    }
    SECTION("an underrun plays silence and then skips, so the clip stays in sync")
    {
        const fs::path longer = makeTake(dir.path(), "l.wav", 80000, 1);
        p.startForTest({{1, longer, 0}}, 0, 48000, warnings);
        // At least 0.5 s was read ahead, in 4096-frame chunks: 6 x 4096 = 24576 frames.
        // Play 30000 without the reader: the rest runs short.
        constexpr std::uint64_t ahead = 6 * 4096;
        std::vector<float> out(30000 * 2, 0.0f);
        p.process(out.data(), 30000, 2, 0);
        CHECK(p.underrunFrames() == 30000 - ahead);
        CHECK(out[(ahead - 1) * 2] == Approx(value(ahead - 1, 0)).margin(1e-6));
        CHECK(out[ahead * 2] == 0.0f);  // ran short: silence
        // After the reader catches up, the next block is the right part of the file again.
        for (int i = 0; i < 30; ++i) p.fillForTest();
        std::vector<float> next(100 * 2, 0.0f);
        p.process(next.data(), 100, 2, 30000);
        CHECK(next[0] == Approx(value(30000, 0)).margin(1e-6));
        CHECK(next[99 * 2] == Approx(value(30099, 0)).margin(1e-6));
        CHECK(p.position() == 30100);
    }
    SECTION("wrong sample rate and missing file: skipped with a message")
    {
        const fs::path at44 = makeTake(dir.path(), "44.wav", 100, 1, 44100);
        p.startForTest({{1, at44, 0}, {2, dir.path() / "gone.wav", 0}, {3, mono, 0}}, 0, 48000, warnings);
        REQUIRE(warnings.size() == 2);
        CHECK(warnings[0].find("44100 Hz") != std::string::npos);
        CHECK(p.length() == 3000);
    }
    SECTION("no clips: the position still runs (recording)")
    {
        p.startForTest({}, 1000, 48000, warnings);
        play(p, 480, 10);
        CHECK(p.position() == 5800);
        CHECK(p.length() == 0);
    }
}

TEST_CASE("T-PLAY-08: player with its reader thread, stop while playing", "[play][T-PLAY-08]")
{
    test::TempDir dir;
    const fs::path take = makeTake(dir.path(), "long.wav", 200000, 2);
    Player p;
    std::vector<std::string> warnings;
    REQUIRE(p.start({{1, take, 0}}, 0, 48000, warnings));
    // Fake audio thread: 1000 blocks of 128 frames while the reader streams.
    std::atomic<bool> bad{false};
    std::thread audio([&] {
        std::vector<float> out(256);
        for (std::uint64_t b = 0; b < 1000; ++b) {
            std::fill(out.begin(), out.end(), 0.0f);
            p.process(out.data(), 128, 2, b * 128);
            const std::uint64_t n = b * 128;
            if (p.underrunFrames() == 0 && std::fabs(out[0] - value(n, 0)) > 1e-5f) bad = true;
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    audio.join();
    CHECK_FALSE(bad);
    CHECK(p.position() == 128000);
    p.stop();
    CHECK(p.takeErrors().empty());
}

namespace {

struct Sent {
    int handle;
    std::vector<std::uint8_t> bytes;
};

class FakeSink : public MidiOutputSink {
public:
    explicit FakeSink(std::vector<Sent>* log) : log_(log) {}
    std::vector<std::string> ports() override { return {"Synth 0", "Piano 1"}; }
    int open(const std::string& name, std::string& error) override
    {
        if (name == "Gone 3") {
            error = "MIDI output \"Gone 3\" was not found.";
            return -1;
        }
        names_.push_back(name);
        return static_cast<int>(names_.size()) - 1;
    }
    void send(int handle, const std::uint8_t* data, std::size_t size) override
    {
        log_->push_back({handle, std::vector<std::uint8_t>(data, data + size)});
    }
    void closeAll() override { names_.clear(); }

private:
    std::vector<Sent>* log_;
    std::vector<std::string> names_;
};

} // namespace

TEST_CASE("T-PLAY-09: MIDI playback: timing, start position, mute, stop", "[play][T-PLAY-09]")
{
    std::vector<Sent> log;
    MidiPlayer mp(std::make_unique<FakeSink>(&log));
    std::vector<std::string> warnings;
    MidiPlaybackTrack a{1, "Synth 0", {{100, {0x90, 60, 100}, 3}, {500, {0x80, 60, 0}, 3}, {900, {0x91, 64, 80}, 3}}, 1000};
    MidiPlaybackTrack b{2, "Piano 1", {{0, {0xC0, 5, 0}, 2}, {50, {0x90, 40, 64}, 3}}, 0};
    MidiPlaybackTrack gone{3, "Gone 3", {{0, {0x90, 1, 1}, 3}}, 0};

    SECTION("events go out when their time comes, per port")
    {
        mp.startForTest({a, b, gone}, 0, warnings);
        REQUIRE(warnings.size() == 1);  // the missing output
        mp.step(40);
        REQUIRE(log.size() == 1);       // program change at 0
        CHECK(log[0].bytes == std::vector<std::uint8_t>{0xC0, 5});
        mp.step(1099);
        CHECK(log.size() == 2);         // + piano note at 50
        mp.step(1100);
        REQUIRE(log.size() == 3);       // synth note at 1000 + 100
        CHECK(log[2].handle == 0);
        CHECK(log[2].bytes == std::vector<std::uint8_t>{0x90, 60, 100});
        mp.step(5000);
        CHECK(mp.sent() == 5);
        log.clear();
        mp.stop();  // the held notes (piano 40, synth 64 on ch 2) are released; all notes off
        auto has = [&](std::vector<std::uint8_t> m) {
            return std::any_of(log.begin(), log.end(), [&](const Sent& s) { return s.bytes == m; });
        };
        CHECK(has({0x80, 40, 0}));
        CHECK(has({0x81, 64, 0}));
        CHECK(has({0xB0, 123, 0}));
        CHECK(has({0xB1, 123, 0}));
        CHECK(has({0xB1, 64, 0}));
        CHECK_FALSE(has({0x80, 60, 0}));  // already released by the file
    }
    SECTION("starting in the middle skips earlier events")
    {
        mp.startForTest({a}, 1400, warnings);
        mp.step(1950);
        REQUIRE(log.size() == 1);
        CHECK(log[0].bytes == std::vector<std::uint8_t>{0x91, 64, 80});
        mp.step(3000);
        CHECK(log.size() == 1);  // the note-off at 1500 belonged to a note never sent: not sent
    }
    SECTION("mute releases held notes and sends no new ones; volume scales velocity")
    {
        mp.setGain(1, 0.5f);
        mp.startForTest({a}, 0, warnings);
        mp.step(1100);
        REQUIRE(log.size() == 1);
        CHECK(log[0].bytes[2] == 50);  // 100 x 0.5
        mp.setGain(1, 0.0f);
        mp.step(1200);
        REQUIRE(log.size() == 2);
        CHECK(log[1].bytes == std::vector<std::uint8_t>{0x80, 60, 0});  // released at mute
        mp.step(5000);
        CHECK(log.size() == 2);  // its own note-off and the next note are not sent
    }
}

TEST_CASE("T-PLAY-14: player stop and start while the audio thread runs", "[play][T-PLAY-14]")
{
    // The hazard pointer under load: the audio thread never sees a freed run.
    test::TempDir dir;
    const fs::path take = makeTake(dir.path(), "t.wav", 48000, 2);
    Player p;
    std::atomic<bool> run{true};
    std::atomic<std::uint64_t> blocks{0};
    std::thread audio([&] {
        std::vector<float> out(512);
        std::uint64_t frame = 0;
        while (run) {
            std::fill(out.begin(), out.end(), 0.0f);
            p.process(out.data(), 256, 2, frame);
            frame += 256;
            ++blocks;
        }
    });
    std::vector<std::string> warnings;
    for (int i = 0; i < 200; ++i) {
        CHECK(p.start({{1, take, 0}}, static_cast<std::uint64_t>(i) * 100, 48000, warnings) == 1);
        std::this_thread::yield();
        if (i % 3 == 0) p.setGain(1, 0.5f, 0.5f);
        p.stop();
    }
    run = false;
    audio.join();
    CHECK(blocks > 0);
    CHECK(warnings.empty());
}

TEST_CASE("T-PLAY-15: player edge cases: one output, three channels, big blocks, no output", "[play][T-PLAY-15]")
{
    test::TempDir dir;
    const fs::path stereo = makeTake(dir.path(), "s.wav", 30000, 2);
    // Three channels, 16-bit, written by hand (WavWriter only writes 1 or 2): sample n of channel c = (n*3+c)/32768.
    const fs::path three = dir.path() / "3.wav";
    {
        std::ofstream o(three, std::ios::binary);
        auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) o.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
        auto put16 = [&](std::uint16_t v) { o.put(static_cast<char>(v & 0xFF)); o.put(static_cast<char>(v >> 8)); };
        const std::uint32_t frames = 1000;
        o.write("RIFF", 4); put32(36 + frames * 6); o.write("WAVE", 4);
        o.write("fmt ", 4); put32(16); put16(1); put16(3); put32(48000); put32(48000 * 6); put16(6); put16(16);
        o.write("data", 4); put32(frames * 6);
        for (std::uint32_t n = 0; n < frames; ++n) for (std::uint16_t c = 0; c < 3; ++c) put16(static_cast<std::uint16_t>(n * 3 + c));
    }
    Player p;
    std::vector<std::string> warnings;

    SECTION("one output channel: left and right averaged")
    {
        p.startForTest({{1, stereo, 0}}, 10, 48000, warnings);
        p.fillForTest();
        std::vector<float> out(4, 0.0f);
        p.process(out.data(), 4, 1, 0);
        CHECK(out[0] == Approx(0.5f * (value(10, 0) + value(10, 1))).margin(1e-6));
    }
    SECTION("a three-channel file plays its first two channels")
    {
        p.startForTest({{1, three, 0}}, 0, 48000, warnings);
        CHECK(warnings.empty());
        const auto out = play(p, 64, 1);
        CHECK(out[2 * 5] == Approx(15.0f / 32768.0f).margin(1e-7));      // frame 5, channel 1
        CHECK(out[2 * 5 + 1] == Approx(16.0f / 32768.0f).margin(1e-7));  // channel 2 (channel 3 not played)
    }
    SECTION("a block larger than kMaxBlock is played in parts, in order")
    {
        p.startForTest({{1, stereo, 0}}, 0, 48000, warnings);
        for (int i = 0; i < 10; ++i) p.fillForTest();
        const unsigned int n = Player::kMaxBlock + 1000;
        std::vector<float> out(n * 2, 0.0f);
        p.process(out.data(), n, 2, 0);
        CHECK(out[(Player::kMaxBlock + 10) * 2] == Approx(value(Player::kMaxBlock + 10, 0)).margin(1e-6));
        CHECK(p.position() == n);
    }
    SECTION("no output: the timeline still moves and stays in step after an underrun (M7)")
    {
        p.startForTest({{1, stereo, 0}}, 0, 48000, warnings);
        p.process(nullptr, 26000, 0, 0);  // more than was read ahead: a deficit
        for (int i = 0; i < 10; ++i) p.fillForTest();
        std::vector<float> out(2 * 2, 0.0f);
        p.process(out.data(), 2, 2, 26000);
        CHECK(out[0] == Approx(value(26000, 0)).margin(1e-6));
    }
}

TEST_CASE("T-PLAY-16: where a new take goes on the timeline (overdub)", "[play][T-PLAY-16]")
{
    LatencyEstimate lat;
    lat.audioFrames = 1000;
    lat.midiFrames = 600;
    CHECK(placeRecordedTake(48000, 0, false, true, lat) == 48000);   // no overdub: where it was recorded
    CHECK(placeRecordedTake(48000, 0, true, true, lat) == 47000);    // audio: earlier by the round trip
    CHECK(placeRecordedTake(48000, 0, true, false, lat) == 47400);   // MIDI: earlier by the output side
    CHECK(placeRecordedTake(std::nullopt, 96000, false, true, lat) == 96000);  // no anchor: where Record began
    CHECK(placeRecordedTake(500, 0, true, true, lat) == -500);       // may start before 0
}

TEST_CASE("T-PLAY-17: MIDI playback: muted tracks keep controllers; tracks do not share held notes",
          "[play][T-PLAY-17]")
{
    std::vector<Sent> log;
    MidiPlayer mp(std::make_unique<FakeSink>(&log));
    std::vector<std::string> warnings;
    MidiPlaybackTrack a{1, "Synth 0", {{0, {0x90, 60, 100}, 3}, {100, {0xB0, 64, 127}, 3}, {200, {0xC0, 7, 0}, 2}}, 0};
    MidiPlaybackTrack b{2, "Synth 0", {{0, {0x90, 60, 90}, 3}, {300, {0x80, 60, 0}, 3}}, 0};
    mp.startForTest({a, b}, 0, warnings);
    mp.step(0);
    REQUIRE(log.size() == 2);  // both note-ons
    mp.setGain(1, 0.0f);       // mute track 1: its note is released ...
    mp.step(50);
    REQUIRE(log.size() == 3);
    CHECK(log[2].bytes == std::vector<std::uint8_t>{0x80, 60, 0});
    mp.step(250);              // ... but its sustain and program change still go out (M5)
    REQUIRE(log.size() == 5);
    CHECK(log[3].bytes == std::vector<std::uint8_t>{0xB0, 64, 127});
    CHECK(log[4].bytes == std::vector<std::uint8_t>{0xC0, 7});
    mp.step(300);              // track 2 still held its note: its note-off is sent (M8)
    REQUIRE(log.size() == 6);
    CHECK(log[5].bytes == std::vector<std::uint8_t>{0x80, 60, 0});
}

TEST_CASE("T-PLAY-05: a WAV header with too many channels is refused", "[play][T-PLAY-05]")
{
    test::TempDir dir;
    const fs::path q = dir.path() / "many.wav";
    std::ofstream o(q, std::ios::binary);
    auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) o.put(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    auto put16 = [&](std::uint16_t v) { o.put(static_cast<char>(v & 0xFF)); o.put(static_cast<char>(v >> 8)); };
    o.write("RIFF", 4); put32(36); o.write("WAVE", 4);
    o.write("fmt ", 4); put32(16); put16(1); put16(16000); put32(48000); put32(48000u * 32000); put16(32000); put16(16);
    o.write("data", 4); put32(0);
    o.close();
    WavReader r;
    std::string err;
    CHECK_FALSE(r.open(q, err));
    CHECK(err.find("channels") != std::string::npos);
}
