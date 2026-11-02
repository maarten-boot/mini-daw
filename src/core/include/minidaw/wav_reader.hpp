#pragma once

// Streaming WAV reader for playback (SPEC-PLAY-02). Used by the playback
// reader thread, never by the audio thread.
//
// Reads PCM 16/24/32-bit and 32-bit float, also WAVE_FORMAT_EXTENSIBLE. If
// the data size in the header is 0 or larger than the file (a take cut short
// by a crash before its header was refreshed), the data runs to the end of
// the file. Other programs may keep writing the file while it is read.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace minidaw {

class WavReader {
public:
    WavReader() = default;
    ~WavReader();
    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    bool open(const std::filesystem::path& path, std::string& error);
    void close();
    bool isOpen() const { return file_ != nullptr; }

    unsigned int sampleRate() const { return sampleRate_; }
    unsigned int channels() const { return channels_; }
    std::uint64_t frames() const { return frames_; }

    // Next read starts at this frame (clamped to the end).
    bool seek(std::uint64_t frame);
    // Reads up to `count` frames as interleaved floats (-1..1, `channels()`
    // values per frame). Returns the number of frames read; fewer at the end
    // of the data or on a read error (then error() says why).
    std::size_t read(float* out, std::size_t count);
    const std::string& error() const { return error_; }

private:
    std::FILE* file_ = nullptr;
    std::filesystem::path path_;
    unsigned int sampleRate_ = 0;
    unsigned int channels_ = 0;
    unsigned int bits_ = 0;
    bool float_ = false;
    std::uint64_t dataStart_ = 0;   // byte offset of the first sample
    std::uint64_t frames_ = 0;
    std::uint64_t position_ = 0;    // next frame
    std::string error_;
    std::vector<unsigned char> buffer_;
};

} // namespace minidaw
