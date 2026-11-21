// Compact recordings as FLAC (P31, SPEC-REC-10): T-REC-36.

#include "minidaw/audio_export.hpp"
#include "minidaw/audio_reader.hpp"
#include "minidaw/compact.hpp"
#include "minidaw/config.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/timeline.hpp"
#include "minidaw/track.hpp"
#include "minidaw/wav.hpp"
#include "minidaw/wav_reader.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <thread>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

void put16(std::ofstream& f, unsigned v)
{
    f.put(static_cast<char>(v & 0xff));
    f.put(static_cast<char>((v >> 8) & 0xff));
}
void put32(std::ofstream& f, std::uint32_t v)
{
    put16(f, v & 0xffff);
    put16(f, v >> 16);
}

// A PCM WAV written by hand, so every integer value (also -8388608) can be in it.
fs::path rawWav(const fs::path& p, unsigned rate, unsigned channels, unsigned bits, const std::vector<std::int32_t>& s)
{
    std::ofstream f(p, std::ios::binary);
    const std::uint32_t data = static_cast<std::uint32_t>(s.size() * (bits / 8));
    f.write("RIFF", 4);
    put32(f, 36 + data);
    f.write("WAVEfmt ", 8);
    put32(f, 16);
    put16(f, 1);
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * channels * (bits / 8));
    put16(f, channels * (bits / 8));
    put16(f, bits);
    f.write("data", 4);
    put32(f, data);
    for (std::int32_t v : s) {
        const auto u = static_cast<std::uint32_t>(v);
        for (unsigned b = 0; b < bits / 8; ++b) f.put(static_cast<char>((u >> (8 * b)) & 0xff));
    }
    return p;
}

std::vector<std::int32_t> testSamples(std::size_t frames, unsigned channels, unsigned bits)
{
    const std::int32_t top = (1 << (bits - 1)) - 1;
    std::vector<std::int32_t> s(frames * channels);
    for (std::size_t i = 0; i < frames; ++i) {
        for (unsigned c = 0; c < channels; ++c) {
            s[i * channels + c] = static_cast<std::int32_t>(
                std::lround(0.6 * top * std::sin(0.01 * static_cast<double>(i) * (c + 1)) +
                           static_cast<double>((i * 7919 + c) % 97) - 48.0));
        }
    }
    s[0] = -top - 1;  // the extremes
    s[1 % s.size()] = top;
    return s;
}

std::vector<float> readAll(const fs::path& p)
{
    AudioReader r;
    std::string err;
    REQUIRE(r.open(p, err));
    std::vector<float> v(r.frames() * r.channels());
    REQUIRE(r.read(v.data(), r.frames()) == r.frames());
    return v;
}

} // namespace

TEST_CASE("T-REC-36: a WAV take becomes FLAC, exactly, and the WAV goes", "[compact][T-REC-36]")
{
    test::TempDir dir;
    for (unsigned bits : {24u, 16u}) {
        for (unsigned ch : {1u, 2u}) {
            INFO(bits << " bit, " << ch << " channel(s)");
            const fs::path wav = dir.path() / ("01-Take " + std::to_string(bits) + "-" + std::to_string(ch) + ".wav");
            const auto samples = testSamples(30000, ch, bits);
            rawWav(wav, 48000, ch, bits, samples);
            const std::vector<float> before = readAll(wav);
            const auto size = fs::file_size(wav);

            const CompactFileResult r = compactFile(wav);
            INFO(r.error);
            REQUIRE(r.ok);
            CHECK(r.error.empty());
            CHECK_FALSE(fs::exists(wav));
            CHECK(fs::exists(r.flac));
            CHECK(r.flac.extension() == ".flac");
            CHECK(r.before == size);
            CHECK(r.after < size);
            // The same integers, and the same floats as the WAV gave (Play does not change).
            AudioReader f;
            std::string err;
            REQUIRE(f.open(r.flac, err));
            CHECK(f.sampleRate() == 48000);
            CHECK(f.channels() == ch);
            CHECK(f.bitsPerSample() == bits);
            CHECK(f.frames() == 30000);
            std::vector<std::int32_t> back(samples.size());
            REQUIRE(f.readPcm(back.data(), 30000) == 30000);
            CHECK(back == samples);
            CHECK(readAll(r.flac) == before);
            // Seeking: from frame 12345 on, as in the WAV.
            REQUIRE(f.seek(12345));
            std::vector<float> part(100 * ch);
            REQUIRE(f.read(part.data(), 100) == 100);
            CHECK(std::equal(part.begin(), part.end(), before.begin() + 12345 * ch));
            CHECK(f.seek(30000));
            CHECK(f.read(part.data(), 100) == 0);
        }
    }
    // No hidden work file left.
    for (const auto& e : fs::directory_iterator(dir.path())) CHECK(pathToUtf8(e.path().filename())[0] != '.');
}

TEST_CASE("T-REC-36: what is not compacted", "[compact][T-REC-36]")
{
    test::TempDir dir;
    // A float WAV: FLAC holds integers only.
    {
        WavWriter w;  // writes 24-bit; a float WAV by hand instead
        std::ofstream f(dir.path() / "01-Float.wav", std::ios::binary);
        f.write("RIFF", 4);
        put32(f, 36 + 8);
        f.write("WAVEfmt ", 8);
        put32(f, 16);
        put16(f, 3);
        put16(f, 1);
        put32(f, 48000);
        put32(f, 48000 * 4);
        put16(f, 4);
        put16(f, 32);
        f.write("data", 4);
        put32(f, 8);
        const float v[2] = {0.5f, -0.5f};
        f.write(reinterpret_cast<const char*>(v), 8);
    }
    CompactFileResult r = compactFile(dir.path() / "01-Float.wav");
    CHECK_FALSE(r.ok);
    CHECK(r.skipped);
    CHECK(r.error.find("float") != std::string::npos);
    CHECK(fs::exists(dir.path() / "01-Float.wav"));
    // A FLAC of that name is there already: nothing done.
    rawWav(dir.path() / "02-A.wav", 48000, 1, 24, testSamples(100, 1, 24));
    std::ofstream(dir.path() / "02-A.flac") << "x";
    r = compactFile(dir.path() / "02-A.wav");
    CHECK(r.skipped);
    CHECK(fs::exists(dir.path() / "02-A.wav"));
    // Not a WAV at all.
    std::ofstream(dir.path() / "03-B.wav") << "not a wav";
    r = compactFile(dir.path() / "03-B.wav");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
    CHECK(fs::exists(dir.path() / "03-B.wav"));
    // Cancelled: nothing changed.
    rawWav(dir.path() / "04-C.wav", 48000, 2, 24, testSamples(50000, 2, 24));
    std::atomic<bool> cancel{true};
    r = compactFile(dir.path() / "04-C.wav", &cancel);
    CHECK_FALSE(r.ok);
    CHECK(fs::exists(dir.path() / "04-C.wav"));
    CHECK_FALSE(fs::exists(dir.path() / "04-C.flac"));
    for (const auto& e : fs::directory_iterator(dir.path())) CHECK(pathToUtf8(e.path().filename())[0] != '.');
}

TEST_CASE("T-REC-36: the take files of a folder; the Compactor queue", "[compact][T-REC-36]")
{
    test::TempDir dir;
    const fs::path takes = dir.path() / "takes";
    fs::create_directories(takes / "take-20261121-100000");
    fs::create_directories(takes / "auto" / "2026-11-21 10-05-00");
    fs::create_directories(takes / "deleted" / "take-20261120-090000");
    const auto s = testSamples(2000, 1, 24);
    rawWav(takes / "take-20261121-100000" / "01-Vocal.wav", 48000, 1, 24, s);
    rawWav(takes / "take-20261121-100000" / "02-Bass.WAV", 48000, 1, 24, s);
    std::ofstream(takes / "take-20261121-100000" / "03-Keys.mid") << "MThd";
    std::ofstream(takes / "take-20261121-100000" / "notes.wav") << "x";  // not "NN-name"
    rawWav(takes / "auto" / "2026-11-21 10-05-00" / "01-Vocal.wav", 48000, 1, 24, s);
    rawWav(takes / "deleted" / "take-20261120-090000" / "01-Old.wav", 48000, 1, 24, s);
    CHECK(wavTakeFiles(takes / "take-20261121-100000", false).size() == 2);
    const auto all = wavTakeFiles(takes, true);
    REQUIRE(all.size() == 3);  // not the one in "deleted", not "notes.wav"

    Compactor c;
    c.setPaused(true);
    c.add(all);
    c.add(all);  // not twice
    CHECK(c.pending() == 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(c.takeResults().empty());  // paused: nothing started
    c.setPaused(false);
    std::vector<CompactFileResult> done;
    for (int i = 0; i < 500 && done.size() < 3; ++i) {
        for (auto& r : c.takeResults()) done.push_back(std::move(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(done.size() == 3);
    for (const auto& r : done) {
        INFO(r.error);
        CHECK(r.ok);
    }
    CHECK(wavTakeFiles(takes, true).empty());
    CHECK(fs::exists(takes / "deleted" / "take-20261120-090000" / "01-Old.wav"));
    c.stop();

    // The Timeline lists the FLAC takes, with their length.
    const auto recs = listRecordings(takes);
    REQUIRE(recs.size() == 2);
    const auto has = [&](const char* name) {
        return std::any_of(recs[0].files.begin(), recs[0].files.end(),
                           [&](const fs::path& f) { return f.filename() == name; });
    };
    CHECK(has("01-Vocal.flac"));
    CHECK(has("02-Bass.flac"));
    CHECK(has("03-Keys.mid"));
    CHECK(recs[0].seconds > 0.04);
    // The audio export reads a FLAC part.
    std::vector<ExportPart> parts(1);
    parts[0].wav = takes / "take-20261121-100000" / "01-Vocal.flac";
    CHECK(mixLength(parts) == 2000);
}

TEST_CASE("T-REC-36: compact in [recording], default on", "[compact][config][T-REC-36]")
{
    CHECK(parseConfig("").config.recording.compact);
    const auto def = parseConfig(defaultConfigText());
    CHECK(def.warnings.empty());
    CHECK(def.config.recording.compact);
    CHECK_FALSE(parseConfig("[recording]\ncompact = false\n").config.recording.compact);
    const auto bad = parseConfig("[recording]\ncompact = maybe\n");
    CHECK(bad.config.recording.compact);
    CHECK(bad.warnings.size() == 1);
    test::TempDir dir;
    AppConfig c;
    c.recording.compact = false;
    REQUIRE(saveConfigFile(dir.path() / "c.ini", c).empty());
    CHECK_FALSE(loadConfigFile(dir.path() / "c.ini").config.recording.compact);
}

// --- Crosscheck #27 fixes (P31a) ------------------------------------------------------

TEST_CASE("T-REC-36: a WAV whose header is shorter than its data keeps all of it (CC1)", "[compact][T-REC-36]")
{
    test::TempDir dir;
    const auto s = testSamples(20000, 2, 24);
    // A crashed take: the header was last refreshed at 12000 frames.
    const fs::path cut = rawWav(dir.path() / "01-Cut.wav", 48000, 2, 24, s);
    {
        std::fstream f(cut, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(40);
        const std::uint32_t d = 12000 * 6;
        for (int b = 0; b < 4; ++b) f.put(static_cast<char>((d >> (8 * b)) & 0xff));
    }
    {
        AudioReader r;
        std::string err;
        REQUIRE(r.open(cut, err));
        CHECK(r.frames() == 20000);  // the file, not the stale header
    }
    const CompactFileResult r = compactFile(cut);
    INFO(r.error);
    REQUIRE(r.ok);
    AudioReader f;
    std::string err;
    REQUIRE(f.open(r.flac, err));
    REQUIRE(f.frames() == 20000);
    std::vector<std::int32_t> back(s.size());
    REQUIRE(f.readPcm(back.data(), 20000) == 20000);
    CHECK(back == s);

    // A WAV with a chunk after its data (LIST, as other programs write): the
    // header's size counts, the chunk is no sound.
    const auto t = testSamples(1000, 1, 16);
    const fs::path listed = rawWav(dir.path() / "02-Listed.wav", 48000, 1, 16, t);
    {
        std::ofstream o(listed, std::ios::binary | std::ios::app);
        o.write("LIST", 4);
        put32(o, 4);
        o.write("INFO", 4);
    }
    AudioReader l;
    REQUIRE(l.open(listed, err));
    CHECK(l.frames() == 1000);
}

TEST_CASE("T-REC-36: render again after compaction gets a new name (CC2)", "[compact][T-REC-36]")
{
    test::TempDir dir;
    Track linked;
    linked.number = 2;
    linked.name = "Keys sound";
    linked.kind = TrackKind::Audio;
    std::ofstream(dir.path() / "02-Keys sound-render.flac") << "x";
    CHECK(newRenderPath(dir.path(), linked).filename() == "02-Keys sound-render-2.wav");
    std::ofstream(dir.path() / "02-Keys sound-render-2.wav") << "x";
    CHECK(newRenderPath(dir.path(), linked).filename() == "02-Keys sound-render-3.wav");
}

TEST_CASE("T-REC-36: FLAC reading as the Player does it, and its edges (CC3, CC5, CC9, CC12)", "[compact][T-REC-36]")
{
    test::TempDir dir;
    const auto s = testSamples(40000, 2, 24);
    const fs::path wav = rawWav(dir.path() / "01-Play.wav", 48000, 2, 24, s);
    const std::vector<float> before = readAll(wav);
    const CompactFileResult c = compactFile(wav);
    REQUIRE(c.ok);

    // CC3: a seek to where it is before every block, and short skips forward.
    AudioReader f;
    std::string err;
    REQUIRE(f.open(c.flac, err));
    std::vector<float> got;
    std::vector<float> block(4096 * 2);
    std::uint64_t pos = 0;
    while (pos < 40000) {
        REQUIRE(f.seek(pos));
        const std::size_t n = f.read(block.data(), 4096);
        REQUIRE(n > 0);
        got.insert(got.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n * 2));
        pos += n;
    }
    CHECK(got == before);
    REQUIRE(f.seek(100));
    REQUIRE(f.read(block.data(), 10) == 10);
    REQUIRE(f.seek(150));  // inside what is decoded already
    REQUIRE(f.read(block.data(), 10) == 10);
    CHECK(std::equal(block.begin(), block.begin() + 20, before.begin() + 150 * 2));
    REQUIRE(f.seek(30000));  // further on, and back
    REQUIRE(f.read(block.data(), 10) == 10);
    CHECK(std::equal(block.begin(), block.begin() + 20, before.begin() + 30000 * 2));
    REQUIRE(f.seek(5));
    REQUIRE(f.read(block.data(), 10) == 10);
    CHECK(std::equal(block.begin(), block.begin() + 20, before.begin() + 5 * 2));

    // CC9: nothing of the file after close().
    f.close();
    CHECK(f.frames() == 0);
    CHECK(f.channels() == 0);
    std::int32_t pcm[8];
    CHECK(f.readPcm(pcm, 4) == 0);

    // CC5: the take's old WAV path opens the FLAC.
    REQUIRE(f.open(wav, err));
    CHECK(f.frames() == 40000);
    f.close();
    CHECK(sameTakePath(dir.path() / "x" / ".." / "01-Play.wav", dir.path() / "01-Play.wav"));
    CHECK_FALSE(sameTakePath(dir.path() / "01-Play.wav", dir.path() / "02-Play.wav"));

    // CC12: a FLAC whose header has no length (total samples 0).
    const fs::path nolen = dir.path() / "02-NoLength.flac";
    fs::copy_file(c.flac, nolen);
    {
        std::fstream b(nolen, std::ios::in | std::ios::out | std::ios::binary);
        b.seekg(21);
        const int v = b.get();
        b.seekp(21);
        b.put(static_cast<char>(v & 0xf0));  // the high 4 bits of the 36-bit count
        for (int i = 0; i < 4; ++i) b.put(0);
    }
    REQUIRE(f.open(nolen, err));
    CHECK(f.frames() == 40000);
    std::vector<float> all(40000 * 2);
    REQUIRE(f.read(all.data(), 40000) == 40000);
    CHECK(all == before);
}

TEST_CASE("T-REC-36: left-over work files, unreadable WAV formats, same names (CC7, CC10, CC11)", "[compact][T-REC-36]")
{
    test::TempDir dir;
    // CC10: a ".part" left by a crash goes with the next compaction there.
    std::ofstream(dir.path() / ".09-Old.flac.part") << "half";
    rawWav(dir.path() / "01-A.wav", 48000, 1, 16, testSamples(500, 1, 16));
    CHECK(compactFile(dir.path() / "01-A.wav").ok);
    CHECK_FALSE(fs::exists(dir.path() / ".09-Old.flac.part"));

    // CC11: an 8-bit WAV is skipped, not a failure.
    {
        std::ofstream f(dir.path() / "02-Eight.wav", std::ios::binary);
        f.write("RIFF", 4);
        put32(f, 36 + 4);
        f.write("WAVEfmt ", 8);
        put32(f, 16);
        put16(f, 1);
        put16(f, 1);
        put32(f, 8000);
        put32(f, 8000);
        put16(f, 1);
        put16(f, 8);
        f.write("data", 4);
        put32(f, 4);
        f.write("\x80\x81\x82\x83", 4);
    }
    const CompactFileResult e = compactFile(dir.path() / "02-Eight.wav");
    CHECK_FALSE(e.ok);
    CHECK(e.skipped);
    CHECK(fs::exists(dir.path() / "02-Eight.wav"));

    // CC7: files of one name in two folders are both queued.
    fs::create_directories(dir.path() / "t1");
    fs::create_directories(dir.path() / "t2");
    const auto s = testSamples(60000, 2, 24);
    const fs::path a = rawWav(dir.path() / "t1" / "01-Vocal.wav", 48000, 2, 24, s);
    const fs::path b = rawWav(dir.path() / "t2" / "01-Vocal.wav", 48000, 2, 24, s);
    Compactor c;
    c.add({a});
    c.add({b});
    c.add({a});  // queued or being done already
    std::vector<CompactFileResult> done;
    for (int i = 0; i < 500 && done.size() < 2; ++i) {
        for (auto& r : c.takeResults()) done.push_back(std::move(r));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (auto& r : c.takeResults()) done.push_back(std::move(r));
    c.stop();
    CHECK(fs::exists(dir.path() / "t1" / "01-Vocal.flac"));
    CHECK(fs::exists(dir.path() / "t2" / "01-Vocal.flac"));
    // The third add either found it queued (2 results) or done (then it was
    // skipped: no WAV any more, an error line) — never converted twice.
    CHECK(done.size() >= 2);
}
