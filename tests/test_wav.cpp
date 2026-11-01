// WAV writer and reader (SPEC-REC-03, SPEC-REC-05a): T-REC-02, T-REC-07.

#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

using namespace minidaw;

namespace {

std::vector<unsigned char> fileBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t u32(const std::vector<unsigned char>& b, std::size_t at)
{
    return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (static_cast<std::uint32_t>(b[at + 3]) << 24);
}

std::uint16_t u16(const std::vector<unsigned char>& b, std::size_t at)
{
    return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}

} // namespace

TEST_CASE("T-REC-02: a 24-bit stereo sine is written and read back", "[wav][T-REC-02]")
{
    test::TempDir dir;
    const auto path = dir / "sine.wav";
    constexpr unsigned int rate = 48000;
    constexpr std::size_t frames = rate;  // 1 second
    std::vector<float> samples(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const float v = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 440.0 * double(i) / rate));
        samples[2 * i] = v;
        samples[2 * i + 1] = -v;
    }

    WavWriter w;
    std::string err;
    REQUIRE(w.open(path, rate, 2, err));
    // Written in uneven blocks, like the disk thread does.
    std::size_t done = 0;
    for (std::size_t block : {1000u, 1u, 30000u, 16999u}) {
        REQUIRE(w.write(samples.data() + done * 2, block, err));
        done += block;
    }
    REQUIRE(done == frames);
    REQUIRE(w.close(err));

    const auto b = fileBytes(path);
    REQUIRE(b.size() == 44 + frames * 6);
    CHECK(std::memcmp(b.data(), "RIFF", 4) == 0);
    CHECK(u32(b, 4) == b.size() - 8);
    CHECK(std::memcmp(b.data() + 8, "WAVEfmt ", 8) == 0);
    CHECK(u16(b, 20) == 1);        // PCM
    CHECK(u16(b, 22) == 2);        // channels
    CHECK(u32(b, 24) == rate);
    CHECK(u32(b, 28) == rate * 6); // byte rate
    CHECK(u16(b, 32) == 6);        // block align
    CHECK(u16(b, 34) == 24);       // bits
    CHECK(u32(b, 40) == frames * 6);

    WavData d;
    REQUIRE(readWav(path, d, err));
    CHECK(d.sampleRate == rate);
    CHECK(d.channels == 2);
    CHECK(d.bitsPerSample == 24);
    REQUIRE(d.frames() == frames);
    double worst = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) worst = std::max(worst, std::fabs(double(d.samples[i]) - samples[i]));
    CHECK(worst <= 1.0 / 8388607.0);  // within one 24-bit step
}

TEST_CASE("24-bit conversion: full scale, clipping and NaN", "[wav][T-REC-02]")
{
    bool clipped = false;
    CHECK(floatToPcm24(0.0f) == 0);
    CHECK(floatToPcm24(1.0f, &clipped) == 8388607);
    CHECK_FALSE(clipped);
    CHECK(floatToPcm24(-1.0f) == -8388607);
    CHECK(floatToPcm24(1.5f, &clipped) == 8388607);
    CHECK(clipped);
    CHECK(floatToPcm24(-7.0f, &clipped) == -8388607);
    CHECK(clipped);
    CHECK(floatToPcm24(std::nanf("")) == 0);
}

TEST_CASE("Clipped samples are counted", "[wav][T-REC-02]")
{
    test::TempDir dir;
    WavWriter w;
    std::string err;
    REQUIRE(w.open(dir / "clip.wav", 44100, 1, err));
    const float s[] = {0.1f, 1.2f, -3.0f, 0.9f};
    REQUIRE(w.write(s, 4, err));
    CHECK(w.clippedSamples() == 2);
    REQUIRE(w.close(err));
}

TEST_CASE("Mono file with an odd data size gets a padding byte", "[wav][T-REC-02]")
{
    test::TempDir dir;
    const auto path = dir / "odd.wav";
    WavWriter w;
    std::string err;
    REQUIRE(w.open(path, 44100, 1, err));
    const float s[] = {0.25f, -0.25f, 0.5f};  // 9 data bytes
    REQUIRE(w.write(s, 3, err));
    REQUIRE(w.close(err));
    const auto b = fileBytes(path);
    REQUIRE(b.size() == 44 + 9 + 1);
    CHECK(u32(b, 40) == 9);              // data size excludes the pad
    CHECK(u32(b, 4) == b.size() - 8);    // RIFF size includes it
    WavData d;
    REQUIRE(readWav(path, d, err));
    CHECK(d.frames() == 3);
    CHECK(d.samples[2] == Catch::Approx(0.5).margin(1e-6));
}

TEST_CASE("T-REC-07: the header is valid while the file is still being written", "[wav][T-REC-07]")
{
    test::TempDir dir;
    const auto path = dir / "live.wav";
    WavWriter w;
    std::string err;
    REQUIRE(w.open(path, 48000, 2, err));
    std::vector<float> block(2 * 4800, 0.25f);
    REQUIRE(w.write(block.data(), 4800, err));
    REQUIRE(w.updateHeader(err));
    REQUIRE(w.write(block.data(), 4800, err));  // not yet in the header

    // Another reader (or the user after a crash) sees a valid file with the
    // audio up to the last header update.
    WavData d;
    REQUIRE(readWav(path, d, err));
    CHECK(d.frames() == 4800);
    REQUIRE(w.close(err));
    REQUIRE(readWav(path, d, err));
    CHECK(d.frames() == 9600);
}

TEST_CASE("WAV writer rejects bad settings and paths", "[wav][T-REC-02]")
{
    test::TempDir dir;
    WavWriter w;
    std::string err;
    CHECK_FALSE(w.open(dir / "x.wav", 48000, 3, err));
    CHECK(err.find("1 or 2 channels") != std::string::npos);
    CHECK_FALSE(w.open(dir / "x.wav", 0, 1, err));
    CHECK_FALSE(w.open(dir / "no-such-folder" / "x.wav", 48000, 1, err));
    CHECK(err.find("Creating") != std::string::npos);
}

TEST_CASE("readWav reads 16-bit PCM and 32-bit float files", "[wav]")
{
    test::TempDir dir;
    auto writeFile = [](const std::filesystem::path& p, std::uint16_t format, std::uint16_t bits,
                        const std::vector<unsigned char>& data) {
        std::vector<unsigned char> h(44);
        auto p32 = [&](std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) h[at + i] = (v >> (8 * i)) & 0xFF; };
        auto p16 = [&](std::size_t at, std::uint16_t v) { h[at] = static_cast<unsigned char>(v & 0xFF); h[at + 1] = static_cast<unsigned char>(v >> 8); };
        std::memcpy(&h[0], "RIFF", 4);
        p32(4, static_cast<std::uint32_t>(36 + data.size()));
        std::memcpy(&h[8], "WAVEfmt ", 8);
        p32(16, 16);
        p16(20, format);
        p16(22, 1);
        p32(24, 8000);
        p32(28, 8000u * bits / 8);
        p16(32, static_cast<std::uint16_t>(bits / 8));
        p16(34, bits);
        std::memcpy(&h[36], "data", 4);
        p32(40, static_cast<std::uint32_t>(data.size()));
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(h.data()), 44);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    };

    std::string err;
    WavData d;
    writeFile(dir / "s16.wav", 1, 16, {0x00, 0x40, 0x00, 0xC0});  // +0.5, -0.5
    REQUIRE(readWav(dir / "s16.wav", d, err));
    REQUIRE(d.frames() == 2);
    CHECK(d.samples[0] == Catch::Approx(0.5));
    CHECK(d.samples[1] == Catch::Approx(-0.5));

    float f[2] = {0.75f, -0.125f};
    std::vector<unsigned char> raw(8);
    std::memcpy(raw.data(), f, 8);
    writeFile(dir / "f32.wav", 3, 32, raw);
    REQUIRE(readWav(dir / "f32.wav", d, err));
    CHECK(d.isFloat);
    CHECK(d.samples[0] == 0.75f);
    CHECK(d.samples[1] == -0.125f);

    CHECK_FALSE(readWav(dir / "missing.wav", d, err));
}
