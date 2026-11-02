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
