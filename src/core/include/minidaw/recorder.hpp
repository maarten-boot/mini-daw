#pragma once

// Recording engine (SPEC-REC-03, SPEC-REC-05, SPEC-NF-01).
//
// The audio thread calls process() with each input block. process() only
// copies the wanted channels into one lock-free ring buffer per take. A disk
// thread empties the rings into WAV files and refreshes the WAV headers about
// once a second. If a ring is full (the disk is too slow), the block is
// dropped and counted: data loss is always reported, never silent.
//
// All takes start in the same audio block, so they are sample-aligned. A
// dropped block is replaced by the same number of frames of silence in the
// file (the audio thread passes a gap marker to the disk thread), so every
// take keeps the session timeline even after a loss (SPEC-REC-02, 05b).

#include "minidaw/ring_buffer.hpp"
#include "minidaw/wav.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

struct TakeSpec {
    std::filesystem::path path;           // WAV file to write
    std::vector<unsigned int> channels;   // input channels (0-based), 1 or 2
    // P11 (SPEC-VST-03a): > 0 = record the stereo sound of this MIDI track's
    // instrument instead of input channels (`channels` is then unused).
    int instrumentTrack = 0;
};

// Audio thread: the sound each instrument made in the current block (P11).
// The InstrumentRack provides it; the Recorder asks after the rack ran.
enum class InstrumentSound {
    Sound,  // left/right hold the block's sound
    None,   // no instrument for that track in this block: silence
    Lost,   // there was sound but it could not be kept (block too long): lost frames
};

class InstrumentSource {
public:
    virtual ~InstrumentSource() = default;
    // The instrument of MIDI track `track` in this block, before the track's
    // gain: `frames` samples per channel.
    virtual InstrumentSound instrumentOutput(int track, unsigned int frames, const float*& left,
                                             const float*& right) const = 0;
};

struct RecorderOptions {
    double ringSeconds = 4.0;         // buffered audio per take before blocks are dropped
    double headerUpdateSeconds = 1.0;  // how often the WAV header is refreshed
    bool syncToDisk = true;           // force the files to disk at each refresh (SPEC-REC-05a)
    bool startDiskThread = true;      // tests can switch this off to force overflows
};

struct TakeResult {
    std::filesystem::path path;
    unsigned int channels = 0;
    std::uint64_t frames = 0;         // file length in frames (lost audio counts as silence)
    std::uint64_t droppedFrames = 0;  // frames of audio that did not reach the file: ring
                                      // full (filled with silence) or disk error
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

    // Audio thread only. `input` is interleaved with `channels` channels
    // (null: no input device; input takes then count the block as lost);
    // `blockFrame` is the stream frame of its first sample; `instruments`
    // gives the instrument takes their sound (null: silence). No locks, no
    // allocation, no I/O.
    void process(const float* input, unsigned int frames, unsigned int channels, std::uint64_t blockFrame = 0,
                 const InstrumentSource* instruments = nullptr);

    // Stream frame of the first recorded sample, once the first block arrived.
    std::optional<std::uint64_t> startFrame() const
    {
        if (!haveStart_.load(std::memory_order_acquire)) return std::nullopt;
        return startFrame_.load(std::memory_order_relaxed);
    }

    // Progress while recording. Safe from the thread that calls start() and
    // stop() (the GUI thread), not from a third thread: they read takes_.
    std::uint64_t framesCaptured() const { return framesCaptured_.load(std::memory_order_relaxed); }
    std::uint64_t droppedFrames() const;
    std::string diskError() const;

    // Tests only, and only with startDiskThread = false: do one disk pass now.
    void drainForTest();
    // Tests only (T-REC-14), with no disk thread running: the next file write
    // fails, for every take or only for take `index`.
    void failNextDiskWriteForTest()
    {
        for (auto& t : takes_) t->writer.failNextWriteForTest();
    }
    void failNextDiskWriteForTest(std::size_t index)
    {
        if (index < takes_.size()) takes_[index]->writer.failNextWriteForTest();
    }

private:
    // A run of dropped frames, placed after `at` frames that went through the ring.
    struct Gap {
        std::uint64_t at = 0;
        std::uint64_t length = 0;
    };

    struct Take {
        TakeSpec spec;
        unsigned int channels = 0;
        std::unique_ptr<SpscRing<float>> ring;
        std::unique_ptr<SpscRing<Gap>> gaps;
        WavWriter writer;
        std::atomic<std::uint64_t> dropped{0};      // ring full (audio thread)
        std::atomic<std::uint64_t> lostOnDisk{0};   // read but not written (disk thread)
        // Audio thread only:
        std::uint64_t queued = 0;                   // frames put into the ring
        std::uint64_t openGap = 0;                  // dropped frames not yet announced
        // Disk thread only (and stop() after the disk thread has ended):
        std::uint64_t ringRead = 0;                 // frames taken from the ring
        Gap pendingGap;
        bool hasPendingGap = false;
        std::uint64_t silenceWritten = 0;
    };

    void diskLoop();
    // Moves buffered audio of every take to its file; returns frames moved.
    std::size_t drainOnce(std::vector<float>& scratch);
    bool writeSilence(Take& t, std::uint64_t frames);
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
    std::atomic<std::uint64_t> startFrame_{0};
    std::atomic<bool> haveStart_{false};

    std::atomic<bool> diskRun_{false};
    std::thread disk_;
    mutable std::mutex errorMutex_;
    std::string diskError_;
};

} // namespace minidaw
