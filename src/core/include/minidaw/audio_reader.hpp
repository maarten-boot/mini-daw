#pragma once
// Reads a take's audio file, WAV or FLAC (P31: recordings may be compacted
// as FLAC). The same interface as WavReader; a FLAC file gives exactly the
// samples of the WAV it was made from (same scaling to floats).
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace minidaw {

// ".wav" or ".flac" (any case): a take's audio file.
bool isAudioTakeFile(const std::filesystem::path& file);
bool isFlacFile(const std::filesystem::path& file);

class AudioReader {
public:
    AudioReader();
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;

    enum class Kind { ByExtension, Wav, Flac };
    // By the extension: ".flac" with libFLAC, anything else as WAV (or as told,
    // e.g. a FLAC file still under a temporary name). By the extension, a
    // ".wav" that is gone while the ".flac" of that name is there opens the
    // FLAC (a compacted take).
    bool open(const std::filesystem::path& path, std::string& error, Kind kind = Kind::ByExtension);
    void close();
    bool isOpen() const;
    unsigned int sampleRate() const;
    unsigned int channels() const;
    unsigned int bitsPerSample() const;
    bool isFloat() const;  // a float WAV (FLAC never)
    std::uint64_t frames() const;
    bool seek(std::uint64_t frame);
    // Interleaved floats, -1 .. 1 (as WavReader scales them).
    std::size_t read(float* out, std::size_t count);
    // Integer samples as stored (not for float WAV files).
    std::size_t readPcm(std::int32_t* out, std::size_t count);
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace minidaw
