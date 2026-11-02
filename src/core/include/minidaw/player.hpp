#pragma once

// Audio playback (SPEC-PLAY-01, 02, 04).
//
// A playback "run" holds one clip per audio track with a take: the WAV file
// and where its first frame sits on the timeline. A reader thread streams
// each clip from disk into a lock-free ring, about 2 seconds ahead, and fills
// silence before a clip starts and after it ends, so every ring holds the
// same stretch of the timeline. The audio callback takes one block from each
// ring, applies the track's gain (ramped over the block) and adds it to the
// output. If a ring runs short (disk too slow), silence is played and the
// missing frames are skipped when they arrive, so the clips stay in sync.
//
// The timeline position advances with each block played. The stream frame
// of the first block and the timeline position there ("anchor") map stream
// frames to the timeline, for MIDI playback and overdub recording.
//
// Thread rules: start(), stop(), setGain() and the getters on the GUI thread;
// process() on the audio thread (no locks, no allocation, no I/O). stop()
// waits until the audio thread no longer uses the run (hazard pointer).

#include "minidaw/ring_buffer.hpp"
#include "minidaw/wav_reader.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

struct PlaybackClip {
    int track = 0;                    // track number (gains are set per track)
    std::filesystem::path file;       // WAV, 1 or 2 channels (more: the first two are played)
    std::int64_t start = 0;           // timeline frame of the file's first frame (may be negative)
};

// Latency estimate for MIDI playback and overdub alignment (SPEC-PLAY-03, 05).
// `reportedFrames` is the driver's stream latency (input + output).
struct LatencyEstimate {
    std::int64_t midiFrames = 0;      // sound for timeline T is heard this much after its block's stream frame
    std::int64_t audioFrames = 0;     // round trip: a recorded take is this much late
};
LatencyEstimate estimateLatency(unsigned int bufferFrames, unsigned int reportedFrames, double correctionMs,
                                unsigned int sampleRate);

// Where a new take goes on the timeline (SPEC-PLAY-05): at the timeline
// position of its first frame (or, if that is unknown because the recording
// stopped before the first block, where recording started), moved earlier
// during an overdub by the audio or MIDI latency estimate.
std::int64_t placeRecordedTake(std::optional<std::int64_t> timelineOfFirstFrame, std::uint64_t recordStartPosition,
                               bool overdub, bool audio, const LatencyEstimate& latency);

class Player {
public:
    static constexpr double kAheadSeconds = 2.0;     // ring size per clip
    static constexpr double kPrefillSeconds = 0.5;   // filled before playing starts
    static constexpr unsigned int kMaxBlock = 8192;  // larger blocks are processed in parts

    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // GUI thread. Opens the clips and starts playing at `position` from the
    // next audio block. Clips whose sample rate differs from `sampleRate`, or
    // that cannot be opened, are skipped with a message in `warnings`.
    // An empty clip list is allowed (the position still runs: recording).
    // Returns how many clips are playing.
    std::size_t start(const std::vector<PlaybackClip>& clips, std::uint64_t position, unsigned int sampleRate,
                      std::vector<std::string>& warnings);
    void stop();
    bool playing() const { return run_.load(std::memory_order_acquire) != nullptr; }
    // While stopped: where the next start() would begin (return to start, position slider).
    void locate(std::uint64_t position)
    {
        if (!playing()) stoppedAt_.store(position, std::memory_order_relaxed);
    }

    // Timeline position of the next block (while playing), or where it stopped.
    std::uint64_t position() const;
    // End of the longest clip of the current (or last) run.
    std::uint64_t length() const { return length_.load(std::memory_order_relaxed); }
    // Plain playback has played all clips to their end.
    bool finished() const { return playing() && position() >= length(); }
    // (stream frame, timeline frame) of the first block played; empty before it.
    std::optional<std::pair<std::uint64_t, std::uint64_t>> anchor() const;
    // Timeline frame of a stream frame, using the anchor (may be negative).
    std::optional<std::int64_t> timelineAt(std::uint64_t streamFrame) const;

    // Linear gains for one track (0 = silent: mute, or not soloed). Any time.
    void setGain(int track, float left, float right);
    std::uint64_t underrunFrames() const;
    std::vector<std::string> takeErrors();

    // Audio thread: adds the clips to `out` (interleaved, 1 or 2 channels).
    void process(float* out, unsigned int frames, unsigned int outChannels, std::uint64_t blockFrame);

    // Tests: run the reader once (fill the rings), instead of its thread.
    void startForTest(const std::vector<PlaybackClip>& clips, std::uint64_t position, unsigned int sampleRate,
                      std::vector<std::string>& warnings);
    void fillForTest();

private:
    struct Clip;
    struct Run;
    std::size_t startRun(const std::vector<PlaybackClip>& clips, std::uint64_t position, unsigned int sampleRate,
                         std::vector<std::string>& warnings, bool thread);
    static void fill(Run& run);   // reader: top up every ring
    void readerLoop(Run* run);

    std::atomic<Run*> run_{nullptr};
    std::atomic<Run*> inUse_{nullptr};   // the run the audio thread is using (hazard pointer)
    std::unique_ptr<Run> owned_;         // GUI thread: owns the published run
    std::map<int, std::pair<float, float>> gains_;  // GUI thread: per track, also for the next run
    std::thread reader_;
    std::atomic<bool> readerRun_{false};
    std::atomic<std::uint64_t> stoppedAt_{0};
    std::atomic<std::uint64_t> length_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::mutex errorsMutex_;
    std::vector<std::string> errors_;
};

} // namespace minidaw
