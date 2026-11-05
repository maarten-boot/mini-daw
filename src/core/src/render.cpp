#include "minidaw/render.hpp"

#include "minidaw/wav.hpp"

#include <algorithm>
#include <cmath>

namespace minidaw {

namespace {
constexpr std::size_t kMaxRenderEvents = 1024;  // per block; more wait for the next block
} // namespace

OfflineRenderer::~OfflineRenderer()
{
    if (active()) {
        cancel();
        finish();
    }
}

bool OfflineRenderer::start(RenderJob job, std::string& error)
{
    if (active()) {
        error = "A render is already running.";
        return false;
    }
    job_ = std::move(job);
    std::sort(job_.events.begin(), job_.events.end(),
              [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });
    if (!instrument_.load(job_.module, job_.classId, job_.sampleRate, error, /*offline=*/true)) return false;
    if ((!job_.state.empty() || !job_.controllerState.empty()) &&
        !instrument_.setState(job_.state, job_.controllerState, error)) {
        instrument_.unload();
        return false;
    }
    done_.store(false);
    cancel_.store(false);
    position_.store(0);
    result_ = {};
    worker_ = std::thread(&OfflineRenderer::run, this);
    return true;
}

double OfflineRenderer::progress() const
{
    if (job_.takeFrames == 0) return done() ? 1.0 : 0.0;
    return std::min(1.0, static_cast<double>(position_.load(std::memory_order_relaxed)) /
                             static_cast<double>(job_.takeFrames));
}

void OfflineRenderer::run()
{
    RenderResult r;
    WavWriter wav;
    std::string err;
    if (!wav.open(job_.out, job_.sampleRate, 2, err)) {
        r.error = err;
    } else {
        const std::uint64_t lastEvent = job_.events.empty() ? 0 : job_.events.back().frame + 1;
        const std::uint64_t end = std::max(job_.takeFrames, lastEvent);
        const auto maxTail = static_cast<std::uint64_t>(job_.maxTailSeconds * job_.sampleRate);
        const auto quietNeeded = static_cast<std::uint64_t>(0.5 * job_.sampleRate);
        constexpr float kSilent = 1.0e-4f;  // -80 dBFS
        std::vector<float> l(kBlock), rr(kBlock), inter(2 * kBlock);
        std::vector<InstrumentMidi> ev;
        ev.reserve(kMaxRenderEvents);
        std::size_t next = 0;
        std::uint64_t pos = 0, quiet = 0;
        bool ok = true;
        while (ok) {
            if (cancel_.load()) {
                r.cancelled = true;
                break;
            }
            if (pos >= end && (quiet >= quietNeeded || pos >= end + maxTail)) break;
            ev.clear();
            while (next < job_.events.size() && job_.events[next].frame < pos + kBlock &&
                   ev.size() < kMaxRenderEvents) {
                const MidiEvent& e = job_.events[next++];
                // An event held back from a full block plays at the start of this one.
                const auto offset = static_cast<std::uint32_t>(e.frame > pos ? e.frame - pos : 0);
                ev.push_back(InstrumentMidi{offset, {e.data[0], e.data[1], e.data[2]}, e.size});
            }
            InstrumentTransport t;
            t.playing = true;
            t.position = static_cast<std::int64_t>(pos);
            instrument_.process(ev.data(), ev.size(), kBlock, t, l.data(), rr.data());
            float blockPeak = 0.0f;
            for (unsigned int f = 0; f < kBlock; ++f) {
                inter[2 * f] = l[f];
                inter[2 * f + 1] = rr[f];
                blockPeak = std::max({blockPeak, std::fabs(l[f]), std::fabs(rr[f])});
            }
            // The last block may go past the end of the sound; it is written whole.
            if (!wav.write(inter.data(), kBlock, err)) {
                r.error = err;
                ok = false;
                break;
            }
            r.peak = std::max(r.peak, blockPeak);
            pos += kBlock;
            r.frames = pos;
            if (pos >= end) quiet = blockPeak < kSilent ? quiet + kBlock : 0;
            position_.store(std::min(pos, end), std::memory_order_relaxed);
        }
        std::string closeErr;
        if (!wav.close(closeErr) && r.error.empty()) r.error = closeErr;
        r.ok = r.error.empty() && !r.cancelled;
    }
    if (!r.ok) {
        std::error_code ec;
        std::filesystem::remove(job_.out, ec);
    }
    result_ = r;
    done_.store(true, std::memory_order_release);
}

RenderResult OfflineRenderer::finish()
{
    if (worker_.joinable()) worker_.join();
    instrument_.unload();  // GUI thread (VST3 rule)
    RenderResult r = result_;
    result_ = {};
    return r;
}

} // namespace minidaw
