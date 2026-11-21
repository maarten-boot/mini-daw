// Compact recordings as FLAC (P31, SPEC-REC-10): T-REC-36.

#include "minidaw/audio_export.hpp"
#include "minidaw/audio_reader.hpp"
#include "minidaw/compact.hpp"
#include "minidaw/config.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/timeline.hpp"
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
