#include "minidaw/wav.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace minidaw {

namespace {

constexpr unsigned int kBytesPerSample = 3;
constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

void put16(unsigned char* p, std::uint16_t v)
{
    p[0] = static_cast<unsigned char>(v);
    p[1] = static_cast<unsigned char>(v >> 8);
}

void put32(unsigned char* p, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(v >> (8 * i));
}

std::uint16_t get16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t get32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

std::int32_t floatToPcm24(float sample, bool* clipped)
{
    constexpr double kScale = 8388607.0;  // 2^23 - 1, symmetric
    double v = sample;
    bool clip = false;
    if (!(v == v)) v = 0.0;  // NaN -> silence
    if (v > 1.0) { v = 1.0; clip = true; }
    if (v < -1.0) { v = -1.0; clip = true; }
    if (clipped) *clipped = clip;
    return static_cast<std::int32_t>(std::lround(v * kScale));
}

// ---------------------------------------------------------------------------
// WavWriter
// ---------------------------------------------------------------------------
WavWriter::~WavWriter()
{
    std::string ignored;
    if (isOpen()) close(ignored);
}

bool WavWriter::fail(const char* what, std::string& error)
{
    error = std::string(what) + " " + path_.string() + " failed";
    if (out_.is_open()) error += " (disk full or no permission?)";
    error += ".";
    return false;
}

bool WavWriter::writeHeader(std::uint64_t dataBytes, bool padded)
{
    std::array<unsigned char, 44> h{};
    const std::uint32_t data32 = static_cast<std::uint32_t>(dataBytes);
    std::memcpy(&h[0], "RIFF", 4);
    put32(&h[4], 36 + data32 + (padded ? 1u : 0u));
    std::memcpy(&h[8], "WAVE", 4);
    std::memcpy(&h[12], "fmt ", 4);
    put32(&h[16], 16);
    put16(&h[20], kFormatPcm);
    put16(&h[22], static_cast<std::uint16_t>(channels_));
    put32(&h[24], sampleRate_);
    put32(&h[28], sampleRate_ * channels_ * kBytesPerSample);
    put16(&h[32], static_cast<std::uint16_t>(channels_ * kBytesPerSample));
    put16(&h[34], 24);
    std::memcpy(&h[36], "data", 4);
    put32(&h[40], data32);
    out_.write(reinterpret_cast<const char*>(h.data()), static_cast<std::streamsize>(h.size()));
    return static_cast<bool>(out_);
}

bool WavWriter::open(const std::filesystem::path& path, unsigned int sampleRate, unsigned int channels,
                     std::string& error)
{
    if (isOpen()) close(error);
    path_ = path;
    if (channels < 1 || channels > 2) {
        error = "WAV writer supports 1 or 2 channels, not " + std::to_string(channels) + ".";
        return false;
    }
    if (sampleRate == 0) {
        error = "WAV writer needs a sample rate.";
        return false;
    }
    sampleRate_ = sampleRate;
    channels_ = channels;
    frames_ = 0;
    clipped_ = 0;
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_.is_open()) return fail("Creating", error);
    if (!writeHeader(0, false)) return fail("Writing", error);
    return true;
}

bool WavWriter::write(const float* interleaved, std::size_t frames, std::string& error)
{
    if (!isOpen()) {
        error = "WAV file is not open.";
        return false;
    }
    const std::size_t samples = frames * channels_;
    const std::uint64_t dataBytes = (frames_ + frames) * channels_ * kBytesPerSample;
    if (dataBytes > kMaxDataBytes) {
        error = "WAV file " + path_.string() + " reached the 4 GiB limit of the format.";
        return false;
    }
    bytes_.resize(samples * kBytesPerSample);
    unsigned char* p = bytes_.data();
    for (std::size_t i = 0; i < samples; ++i) {
        bool clip = false;
        const std::int32_t s = floatToPcm24(interleaved[i], &clip);
        if (clip) ++clipped_;
        const auto u = static_cast<std::uint32_t>(s);
        p[0] = static_cast<unsigned char>(u);
        p[1] = static_cast<unsigned char>(u >> 8);
        p[2] = static_cast<unsigned char>(u >> 16);
        p += kBytesPerSample;
    }
    out_.write(reinterpret_cast<const char*>(bytes_.data()), static_cast<std::streamsize>(bytes_.size()));
    if (!out_) return fail("Writing", error);
    frames_ += frames;
    return true;
}

bool WavWriter::updateHeader(std::string& error)
{
    if (!isOpen()) return true;
    const std::uint64_t dataBytes = frames_ * channels_ * kBytesPerSample;
    const auto end = out_.tellp();
    out_.seekp(0);
    const bool ok = writeHeader(dataBytes, false);
    out_.seekp(end);
    out_.flush();
    if (!ok || !out_) return fail("Updating the header of", error);
    return true;
}

bool WavWriter::close(std::string& error)
{
    if (!isOpen()) return true;
    const std::uint64_t dataBytes = frames_ * channels_ * kBytesPerSample;
    bool ok = true;
    const bool padded = (dataBytes % 2) != 0;  // RIFF chunks are word-aligned
    if (padded) {
        out_.put('\0');
        ok = static_cast<bool>(out_);
    }
    out_.seekp(0);
    ok = ok && writeHeader(dataBytes, padded);
    out_.flush();
    ok = ok && static_cast<bool>(out_);
    out_.close();
    if (!ok) return fail("Finishing", error);
    return true;
}

// ---------------------------------------------------------------------------
// readWav
// ---------------------------------------------------------------------------
bool readWav(const std::filesystem::path& path, WavData& data, std::string& error)
{
    data = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open " + path.string() + ".";
        return false;
    }
    unsigned char riff[12];
    if (!in.read(reinterpret_cast<char*>(riff), 12) || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0) {
        error = path.string() + " is not a WAV file.";
        return false;
    }

    std::uint16_t format = 0;
    std::uint16_t bits = 0;
    bool haveFmt = false;
    for (;;) {
        unsigned char hdr[8];
        if (!in.read(reinterpret_cast<char*>(hdr), 8)) break;
        const std::uint32_t size = get32(hdr + 4);
        if (std::memcmp(hdr, "fmt ", 4) == 0) {
            std::vector<unsigned char> f(size);
            if (size < 16 || !in.read(reinterpret_cast<char*>(f.data()), size)) break;
            format = get16(&f[0]);
            data.channels = get16(&f[2]);
            data.sampleRate = get32(&f[4]);
            bits = get16(&f[14]);
            if (format == kFormatExtensible && size >= 26) format = get16(&f[24]);  // sub-format GUID starts here
            haveFmt = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            if (!haveFmt) break;
            data.bitsPerSample = bits;
            data.isFloat = format == kFormatFloat;
            const bool pcmOk = format == kFormatPcm && (bits == 16 || bits == 24 || bits == 32);
            const bool floatOk = data.isFloat && bits == 32;
            if (!(pcmOk || floatOk) || data.channels == 0) {
                error = path.string() + ": unsupported WAV format.";
                return false;
            }
            const unsigned int bps = bits / 8;
            std::vector<unsigned char> raw(size);
            in.read(reinterpret_cast<char*>(raw.data()), size);
            const std::size_t got = static_cast<std::size_t>(in.gcount());
            const std::size_t count = got / bps;
            data.samples.resize(count - count % data.channels);
            for (std::size_t i = 0; i < data.samples.size(); ++i) {
                const unsigned char* p = &raw[i * bps];
                float v = 0.0f;
                if (data.isFloat) {
                    std::uint32_t u = get32(p);
                    std::memcpy(&v, &u, 4);
                } else if (bits == 16) {
                    v = static_cast<float>(static_cast<std::int16_t>(get16(p))) / 32768.0f;
                } else if (bits == 24) {
                    std::int32_t s = static_cast<std::int32_t>((static_cast<std::uint32_t>(p[0]) << 8) |
                                                               (static_cast<std::uint32_t>(p[1]) << 16) |
                                                               (static_cast<std::uint32_t>(p[2]) << 24)) >> 8;
                    v = static_cast<float>(static_cast<double>(s) / 8388607.0);
                } else {
                    v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(get32(p))) / 2147483648.0);
                }
                data.samples[i] = v;
            }
            return true;
        } else {
            in.seekg(size + (size & 1u), std::ios::cur);
        }
    }
    error = path.string() + ": no audio data found.";
    return false;
}

} // namespace minidaw
