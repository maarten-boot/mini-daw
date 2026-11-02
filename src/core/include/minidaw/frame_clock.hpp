#pragma once

// Audio timeline <-> steady clock (SPEC-REC-04a).
//
// The audio callback publishes, once per block, the frame position at the
// end of the block it just received together with the steady-clock time of
// the callback. Other threads (MIDI input) map a clock time to a frame:
// frame = published frame + (time - published time) x sample rate.
//
// One writer (audio thread), any number of readers. Lock-free: a sequence
// counter guards the pair, and a reader retries while it changes.
// The driver's input latency is not added; T-REC-04 measures the result.
//
// The callback time jitters by up to a buffer (the thread can be woken
// late, never early). ClockSmoother removes most of it before publishing:
// for an ideal stream, "time - frame / rate" is constant; a late callback
// only makes it larger. So the smoother uses the smallest value of the last
// 0.5-1 s (which also follows drift between the audio and system clocks),
// skips the first 0.5 s after a start (drivers often deliver the first
// blocks in a burst), and starts again after a dropout or a large jump.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>

namespace minidaw {

inline std::int64_t steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Nanoseconds of audio for `frame` frames, without overflow for long streams.
inline std::int64_t framesToNs(std::uint64_t frame, unsigned int sampleRate)
{
    const std::uint64_t whole = frame / sampleRate;
    const std::uint64_t rest = frame % sampleRate;
    return static_cast<std::int64_t>(whole * 1'000'000'000ull + rest * 1'000'000'000ull / sampleRate);
}

// Audio thread only (plain state, no atomics needed). SPEC-REC-04a.
class ClockSmoother {
public:
    // The minimum is taken over the current and the previous window, so a
    // value is remembered for 1 to 2 windows (0.5-1 s); this also follows
    // clock drift.
    static constexpr std::int64_t kWindowNs = 500'000'000;   // 0.5 s
    // Drivers often deliver the first blocks in a burst, which would look
    // "early"; for this long after a (re)start the raw times are used.
    static constexpr std::int64_t kWarmupNs = 500'000'000;   // 0.5 s
    // An offset this far above the estimate restarts it (stream glitch).
    static constexpr std::int64_t kMinResetNs = 50'000'000;   // 50 ms

    void reset() { have_ = false; }

    // `frame` was reached at `ns` (callback time); returns the smoothed time
    // for that frame. `glitch` = the driver reported a dropout for this block.
    std::int64_t update(std::uint64_t frame, std::int64_t ns, unsigned int sampleRate, unsigned int bufferFrames,
                        bool glitch)
    {
        if (sampleRate == 0) return ns;
        const std::int64_t audioNs = framesToNs(frame, sampleRate);
        const std::int64_t observed = ns - audioNs;  // constant for an ideal stream, larger when late
        const std::int64_t resetAbove =
            std::max<std::int64_t>(kMinResetNs, 3 * framesToNs(bufferFrames, sampleRate));
        if (!have_ || glitch || observed - estimate() > resetAbove) {
            previousMin_ = currentMin_ = observed;
            windowStart_ = ns;
            startNs_ = ns;
            have_ = true;
        } else if (ns - startNs_ < kWarmupNs) {
            previousMin_ = currentMin_ = observed;  // warm-up: raw time, nothing remembered
            windowStart_ = ns;
        } else {
            if (ns - windowStart_ >= kWindowNs) {
                previousMin_ = currentMin_;
                currentMin_ = observed;
                windowStart_ = ns;
            } else {
                currentMin_ = std::min(currentMin_, observed);
            }
        }
        return estimate() + audioNs;
    }

private:
    std::int64_t estimate() const { return std::min(previousMin_, currentMin_); }

    bool have_ = false;
    std::int64_t previousMin_ = 0;
    std::int64_t currentMin_ = 0;
    std::int64_t windowStart_ = 0;
    std::int64_t startNs_ = 0;
};

class FrameClock {
public:
    // Audio thread only. No locks, no allocation.
    void publish(std::uint64_t frame, std::int64_t ns)
    {
        const std::uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);  // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        frame_.store(frame, std::memory_order_relaxed);
        ns_.store(ns, std::memory_order_relaxed);
        seq_.store(s + 2, std::memory_order_release);  // even: done
    }

    void reset()
    {
        seq_.store(0, std::memory_order_relaxed);
        frame_.store(0, std::memory_order_relaxed);
        ns_.store(0, std::memory_order_relaxed);
    }

    // The latest pair; false if nothing was published yet.
    bool read(std::uint64_t& frame, std::int64_t& ns) const
    {
        for (;;) {
            const std::uint32_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 == 0) return false;
            if (s1 & 1u) continue;
            frame = frame_.load(std::memory_order_relaxed);
            ns = ns_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s1) return true;
        }
    }

    std::optional<std::uint64_t> frameAt(std::int64_t ns, unsigned int sampleRate) const
    {
        std::uint64_t f = 0;
        std::int64_t t = 0;
        if (sampleRate == 0 || !read(f, t)) return std::nullopt;
        return mapToFrame(f, t, ns, sampleRate);
    }

    // Pure mapping (exposed for tests); clamps at frame 0.
    static std::uint64_t mapToFrame(std::uint64_t frame, std::int64_t frameNs, std::int64_t ns,
                                    unsigned int sampleRate)
    {
        const double delta = static_cast<double>(ns - frameNs) * 1e-9 * sampleRate;
        const double result = static_cast<double>(frame) + delta;
        return result <= 0.0 ? 0 : static_cast<std::uint64_t>(result + 0.5);
    }

private:
    std::atomic<std::uint32_t> seq_{0};
    std::atomic<std::uint64_t> frame_{0};
    std::atomic<std::int64_t> ns_{0};
};

} // namespace minidaw
