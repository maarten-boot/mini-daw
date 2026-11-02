#include "minidaw/wav_reader.hpp"

#include "minidaw/path_util.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#include <share.h>
#endif

namespace minidaw {

namespace {

std::uint32_t le32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t le16(const unsigned char* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::FILE* openShared(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return _wfsopen(path.c_str(), L"rb", _SH_DENYNO);  // a take may still be written
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool seekTo(std::FILE* f, std::uint64_t offset)
{
#if defined(_WIN32)
    return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
    return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

} // namespace

WavReader::~WavReader()
{
    close();
}

void WavReader::close()
{
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

bool WavReader::open(const std::filesystem::path& path, std::string& error)
{
    close();
    sampleRate_ = channels_ = bits_ = 0;
    float_ = false;
    dataStart_ = frames_ = position_ = 0;
    error_.clear();
    path_ = path;
    const std::string name = pathToUtf8(path.filename());
    std::error_code ec;
    const std::uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Cannot read " + pathToUtf8(path) + ": " + ec.message();
        return false;
    }
    file_ = openShared(path);
    if (!file_) {
        error = "Cannot open " + pathToUtf8(path) + ".";
        return false;
    }
    unsigned char riff[12];
    if (std::fread(riff, 1, 12, file_) != 12 || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0) {
        error = name + " is not a WAV file.";
        close();
        return false;
    }
    bool haveFmt = false;
    std::uint64_t pos = 12;
    unsigned int blockAlign = 0;
    while (pos + 8 <= fileSize) {
        unsigned char hdr[8];
        if (!seekTo(file_, pos) || std::fread(hdr, 1, 8, file_) != 8) break;
        const std::uint32_t size = le32(hdr + 4);
        if (std::memcmp(hdr, "fmt ", 4) == 0) {
            unsigned char fmt[40] = {};
            const std::size_t n = std::fread(fmt, 1, std::min<std::size_t>(size, sizeof fmt), file_);
            if (n < 16) break;
            std::uint16_t tag = le16(fmt);
            channels_ = le16(fmt + 2);
            sampleRate_ = le32(fmt + 4);
            blockAlign = le16(fmt + 12);
            bits_ = le16(fmt + 14);
            if (tag == 0xFFFE && n >= 26) tag = le16(fmt + 24);  // WAVE_FORMAT_EXTENSIBLE: sub-format
            float_ = tag == 3;
            if (!((tag == 1 && (bits_ == 16 || bits_ == 24 || bits_ == 32)) || (tag == 3 && bits_ == 32))) {
                error = name + ": this WAV format is not supported (only PCM 16/24/32-bit and 32-bit float).";
                close();
                return false;
            }
            if (channels_ == 0 || channels_ > 64) {  // a damaged header must not make us allocate hugely (M9)
                error = name + ": " + std::to_string(channels_) + " channels is not supported (1 to 64).";
                close();
                return false;
            }
            haveFmt = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            if (!haveFmt || channels_ == 0 || blockAlign != channels_ * (bits_ / 8)) {
                error = name + ": the WAV header is damaged.";
                close();
                return false;
            }
            dataStart_ = pos + 8;
            std::uint64_t bytes = size;
            const std::uint64_t rest = fileSize - dataStart_;
            if (bytes == 0 || bytes > rest) bytes = rest;  // header not refreshed (crash): use the file
            frames_ = bytes / blockAlign;
            buffer_.resize(static_cast<std::size_t>(blockAlign) * 4096);
            return seek(0);
        }
        pos += 8 + size + (size & 1u);
    }
    error = name + ": no audio data found.";
    close();
    return false;
}

bool WavReader::seek(std::uint64_t frame)
{
    if (!file_) return false;
    position_ = std::min(frame, frames_);
    if (!seekTo(file_, dataStart_ + position_ * channels_ * (bits_ / 8))) {
        error_ = "Seek failed in " + pathToUtf8(path_.filename()) + ".";
        return false;
    }
    return true;
}

std::size_t WavReader::read(float* out, std::size_t count)
{
    if (!file_ || channels_ == 0) return 0;
    const std::size_t bytesPerFrame = static_cast<std::size_t>(channels_) * (bits_ / 8);
    const std::size_t chunkFrames = buffer_.size() / bytesPerFrame;
    std::size_t done = 0;
    while (done < count && position_ < frames_) {
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(std::min(count - done, chunkFrames), frames_ - position_));
        const std::size_t got = std::fread(buffer_.data(), bytesPerFrame, want, file_);
        const unsigned char* p = buffer_.data();
        float* o = out + done * channels_;
        const std::size_t samples = got * channels_;
        for (std::size_t i = 0; i < samples; ++i) {
            float v = 0.0f;
            if (float_) {
                std::memcpy(&v, p, 4);
                p += 4;
            } else if (bits_ == 16) {
                v = static_cast<float>(static_cast<std::int16_t>(le16(p))) / 32768.0f;
                p += 2;
            } else if (bits_ == 24) {
                std::int32_t s = static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
                if (s & 0x800000) s -= 0x1000000;
                v = static_cast<float>(static_cast<double>(s) / 8388607.0);  // as WavWriter scales
                p += 3;
            } else {
                v = static_cast<float>(static_cast<double>(static_cast<std::int32_t>(le32(p))) / 2147483648.0);
                p += 4;
            }
            o[i] = v;
        }
        done += got;
        position_ += got;
        if (got < want) {
            error_ = "Read error in " + pathToUtf8(path_.filename()) + ".";
            break;
        }
    }
    return done;
}

} // namespace minidaw
