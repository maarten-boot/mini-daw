#pragma once

// WAV files (SPEC-REC-03, SPEC-REC-05a).
//
// WavWriter writes 24-bit PCM, mono or stereo. It is used by the disk thread,
// never by the audio thread. updateHeader() rewrites the sizes in the header,
// so a file that is still being recorded is valid up to the last update; the
// recorder calls it about once a second. With `syncToDisk` it also forces the
// file to the disk (FlushFileBuffers / fsync), so it survives power loss.
// After a write error (disk full), close() cuts the file back to the frames
// that were written and still writes a valid header.
//
// readWav() reads 16/24/32-bit PCM and 32-bit float files; it is used by the
// tests now and by playback later (P8).

#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <string>
#include <vector>

namespace minidaw {

class WavWriter {
public:
    // Largest data chunk a plain RIFF/WAV file can describe (sizes are 32-bit).
    static constexpr std::uint64_t kMaxDataBytes = 0xFFFFFFFFull - 36 - 1;

    WavWriter() = default;
    ~WavWriter();
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    // Creates (or overwrites) the file and writes a header for 0 frames.
    bool open(const std::filesystem::path& path, unsigned int sampleRate, unsigned int channels, std::string& error);

    // Converts interleaved float samples (-1 .. 1, clipped outside) to 24-bit.
    // `interleaved == nullptr` writes `frames` frames of silence.
    // Fails without writing anything if the file would exceed kMaxDataBytes.
    bool write(const float* interleaved, std::size_t frames, std::string& error);

    // Rewrite the size fields and flush to the OS; with syncToDisk also force
    // the data onto the disk.
    bool updateHeader(std::string& error, bool syncToDisk = false);

    // updateHeader() plus the padding byte for an odd-sized data chunk; closes.
    bool close(std::string& error);

    // Keep only the first `frames` frames: close() cuts the rest off (auto
    // record, P16: a part ends at a frame that may already be in the file).
    // No more writes after this.
    void keepOnlyFrames(std::uint64_t frames)
    {
        if (isOpen() && frames < frames_) {
            frames_ = frames;
            broken_ = true;  // close() cuts the file back to frames_ and writes the header
        }
    }

    bool isOpen() const { return file_ != nullptr; }
    std::uint64_t framesWritten() const { return frames_; }
    std::uint64_t clippedSamples() const { return clipped_; }
    const std::filesystem::path& path() const { return path_; }

    // Tests only (T-REC-11): pretend `frames` frames are already in the file,
    // to reach the 4 GiB limit without writing 4 GiB.
    void pretendFramesWrittenForTest(std::uint64_t frames) { frames_ = frames; }
    // Tests only (T-REC-12): the next write() puts half its bytes in the file
    // and then fails, like a disk that fills up in the middle of a block.
    void failNextWriteForTest() { failNextWrite_ = true; }

private:
    bool fail(const char* what, std::string& error);
    bool writeHeader(std::uint64_t dataBytes, bool padded);
    bool syncFile();

    std::FILE* file_ = nullptr;
    bool broken_ = false;               // a write failed; the file end is unknown
    bool failNextWrite_ = false;
    std::filesystem::path path_;
    unsigned int sampleRate_ = 0;
    unsigned int channels_ = 0;
    std::uint64_t frames_ = 0;
    std::uint64_t clipped_ = 0;
    std::vector<unsigned char> bytes_;  // conversion buffer, reused
};

struct WavData {
    unsigned int sampleRate = 0;
    unsigned int channels = 0;
    unsigned int bitsPerSample = 0;
    bool isFloat = false;
    std::vector<float> samples;  // interleaved, -1 .. 1
    std::uint64_t frames() const { return channels ? samples.size() / channels : 0; }
};

bool readWav(const std::filesystem::path& path, WavData& data, std::string& error);

// The 24-bit value written for a float sample (exposed for tests).
std::int32_t floatToPcm24(float sample, bool* clipped = nullptr);

} // namespace minidaw
