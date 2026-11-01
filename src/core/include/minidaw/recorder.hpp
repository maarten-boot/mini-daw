#pragma once

// Recording engine (SPEC-REC-03, SPEC-REC-05, SPEC-NF-01).
//
// The audio thread calls process() with each input block. process() only
// copies the wanted channels into one lock-free ring buffer per take. A disk
// thread empties the rings into WAV files and refreshes the WAV headers about
// once a second. If a ring is full (the disk is too slow), the block is
// dropped and counted: data loss is always reported, never silent.
//
// All takes start in the same audio block, so they are sample-aligned.

#include "minidaw/ring_buffer.hpp"
#include "minidaw/wav.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

struct TakeSpec {
    std::filesystem::path path;           // WAV file to write
    std::vector<unsigned int> channels;   // input channels (0-based), 1 or 2
};

struct RecorderOptions {
    double ringSeconds = 4.0;         // buffered audio per take before blocks are dropped
    double headerUpdateSeconds = 1.0;  // how often the WAV header is refreshed
    bool startDiskThread = true;      // tests can switch this off to force overflows
};

struct TakeResult {
    std::filesystem::path path;
    unsigned int channels = 0;
    std::uint64_t frames = 0;         // frames written to the file
    std::uint64_t droppedFrames = 0;  // frames lost because the ring was full
    std::uint64_t clippedSamples = 0;
};

struct RecordingResult {
    std::vector<TakeResult> takes;
    std::string error;                // first disk error, empty if none
    bool lostData() const;
};

class Recorder {
public:
    Recorder() = default;
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Opens the WAV files and starts the disk thread. `inputChannels` is the
    // channel count of the blocks process() will receive. Fails (with nothing
    // recorded) if a file cannot be created or a channel does not exist.
    bool start(const std::vector<TakeSpec>& takes, unsigned int sampleRate, unsigned int inputChannels,
               std::string& error, const RecorderOptions& options = {});

    // Stops taking audio, writes what is still buffered, closes the files.
    RecordingResult stop();

    bool recording() const { return active_.load(); }

    // Audio thread only. `input` is interleaved with `channels` channels.
    // No locks, no allocation, no I/O.
    void process(const float* input, unsigned int frames, unsigned int channels);

    // Progress, safe to call from the GUI thread while recording.
    std::uint64_t framesCaptured() const { return framesCaptured_.load(std::memory_order_relaxed); }
    std::uint64_t droppedFrames() const;
    std::string diskError() const;

private:
    struct Take {
        TakeSpec spec;
        unsigned int channels = 0;
        std::unique_ptr<SpscRing<float>> ring;
        WavWriter writer;
        std::atomic<std::uint64_t> dropped{0};
    };

    void diskLoop();
    // Moves buffered audio of every take to its file; returns frames moved.
    std::size_t drainOnce(std::vector<float>& scratch);
    void setDiskError(const std::string& text);

    std::vector<std::unique_ptr<Take>> takes_;
    unsigned int inputChannels_ = 0;
    unsigned int sampleRate_ = 0;
    RecorderOptions options_;

    // process() runs only while active_ is true; stop() waits for inProcess_
    // to reach 0 after clearing active_ (both seq_cst).
    std::atomic<bool> active_{false};
    std::atomic<int> inProcess_{0};
    std::atomic<std::uint64_t> framesCaptured_{0};

    std::atomic<bool> diskRun_{false};
    std::thread disk_;
    mutable std::mutex errorMutex_;
    std::string diskError_;
};

} // namespace minidaw
