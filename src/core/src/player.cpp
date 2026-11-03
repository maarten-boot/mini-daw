#include "minidaw/player.hpp"

#include "minidaw/path_util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace minidaw {

LatencyEstimate estimateLatency(unsigned int bufferFrames, unsigned int reportedFrames, double correctionMs,
                                unsigned int sampleRate)
{
    // Sound for a block is heard about one more buffer (double buffering)
    // plus the driver's output latency after the callback that made it; the
    // callback itself runs one buffer after the block's start frame. The
    // driver reports input + output together: half is taken for each.
    LatencyEstimate e;
    const auto buf = static_cast<std::int64_t>(bufferFrames);
    const auto rep = static_cast<std::int64_t>(reportedFrames);
    const auto corr = static_cast<std::int64_t>(std::llround(correctionMs * sampleRate / 1000.0));
    e.midiFrames = 2 * buf + rep / 2;
    e.audioFrames = 2 * buf + rep + corr;
    return e;
}

struct Player::Clip {
    int track = 0;
    WavReader reader;
    unsigned int fileChannels = 0;
    unsigned int channels = 0;           // 1 or 2 (played)
    std::int64_t start = 0;
    std::uint64_t end = 0;               // timeline frame after the last one
    std::unique_ptr<SpscRing<float>> ring;
    std::int64_t produced = 0;           // reader: next timeline frame to put in the ring
    std::vector<float> fileBuf;          // reader
    std::vector<float> scratch;          // audio thread: one block (kMaxBlock x channels)
    std::atomic<float> gainL{1.0f};
    std::atomic<float> gainR{1.0f};
    float curL = 0.0f;                   // audio thread: gains of the last block (ramp from here)
    float curR = 0.0f;
    std::uint64_t deficit = 0;           // audio thread: frames owed after an underrun
    bool failed = false;                 // reader: read error reported
};

struct Player::Run {
    std::vector<std::unique_ptr<Clip>> clips;
    std::uint64_t startPosition = 0;
    std::atomic<std::uint64_t> played{0};
    std::atomic<bool> anchored{false};
    std::atomic<std::uint64_t> anchorStream{0};
    std::vector<std::string>* errors = nullptr;
    std::mutex* errorsMutex = nullptr;
};

Player::Player() = default;

Player::~Player()
{
    stop();
}

std::int64_t placeRecordedTake(std::optional<std::int64_t> timelineOfFirstFrame, std::uint64_t recordStartPosition,
                               bool overdub, bool audio, const LatencyEstimate& latency)
{
    const std::int64_t at = timelineOfFirstFrame ? *timelineOfFirstFrame : static_cast<std::int64_t>(recordStartPosition);
    if (!overdub) return at;
    return at - (audio ? latency.audioFrames : latency.midiFrames);
}

std::size_t Player::start(const std::vector<PlaybackClip>& clips, std::uint64_t position, unsigned int sampleRate,
                          std::vector<std::string>& warnings)
{
    return startRun(clips, position, sampleRate, warnings, true);
}

void Player::startForTest(const std::vector<PlaybackClip>& clips, std::uint64_t position, unsigned int sampleRate,
                          std::vector<std::string>& warnings)
{
    startRun(clips, position, sampleRate, warnings, false);
}

void Player::fillForTest()
{
    if (owned_) fill(*owned_);
}

std::size_t Player::startRun(const std::vector<PlaybackClip>& clips, std::uint64_t position,
                             unsigned int sampleRate, std::vector<std::string>& warnings, bool thread)
{
    stop();
    auto run = std::make_unique<Run>();
    run->startPosition = position;
    run->errors = &errors_;
    run->errorsMutex = &errorsMutex_;
    std::uint64_t length = 0;
    const auto ringFrames = static_cast<std::size_t>(kAheadSeconds * sampleRate);
    for (const auto& c : clips) {
        auto clip = std::make_unique<Clip>();
        std::string err;
        if (!clip->reader.open(c.file, err)) {
            warnings.push_back("Track " + std::to_string(c.track) + ": " + err);
            continue;
        }
        if (clip->reader.sampleRate() != sampleRate) {
            warnings.push_back("Track " + std::to_string(c.track) + ": " + pathToUtf8(c.file.filename()) +
                               " is at " + std::to_string(clip->reader.sampleRate()) + " Hz, the stream at " +
                               std::to_string(sampleRate) + " Hz; it is not played (no resampling yet).");
            continue;
        }
        clip->track = c.track;
        clip->fileChannels = clip->reader.channels();
        clip->channels = std::min(clip->fileChannels, 2u);
        clip->start = c.start;
        const std::int64_t end = c.start + static_cast<std::int64_t>(clip->reader.frames());
        clip->end = end > 0 ? static_cast<std::uint64_t>(end) : 0;
        length = std::max(length, clip->end);
        clip->ring = std::make_unique<SpscRing<float>>(ringFrames * clip->channels);
        clip->produced = static_cast<std::int64_t>(position);
        clip->fileBuf.resize(4096 * static_cast<std::size_t>(clip->fileChannels));
        clip->scratch.resize(static_cast<std::size_t>(kMaxBlock) * clip->channels);
        const auto it = gains_.find(c.track);
        if (it != gains_.end()) {
            clip->gainL.store(it->second.first);
            clip->gainR.store(it->second.second);
        }
        clip->curL = clip->gainL.load();
        clip->curR = clip->gainR.load();
        run->clips.push_back(std::move(clip));
    }
    length_.store(length, std::memory_order_relaxed);
    stoppedAt_.store(position, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);

    // Prefill, so the first blocks do not run short.
    const auto prefill = static_cast<std::int64_t>(kPrefillSeconds * sampleRate);
    for (auto& c : run->clips) {
        while (c->produced < static_cast<std::int64_t>(position) + prefill && c->ring->writeSpace() >= 4096 * c->channels) {
            fill(*run);
        }
    }
    owned_ = std::move(run);
    run_.store(owned_.get(), std::memory_order_seq_cst);
    if (thread) {
        readerRun_.store(true);
        reader_ = std::thread(&Player::readerLoop, this, owned_.get());
    }
    return owned_->clips.size();
}

void Player::stop()
{
    Run* old = run_.exchange(nullptr, std::memory_order_seq_cst);
    if (old) {
        // Wait until the audio thread has left the run (it holds it for at
        // most one block).
        while (inUse_.load(std::memory_order_seq_cst) == old) std::this_thread::yield();
        stoppedAt_.store(old->startPosition + old->played.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    }
    readerRun_.store(false);
    if (reader_.joinable()) reader_.join();
    owned_.reset();
}

std::uint64_t Player::position() const
{
    // The run cannot be freed while the GUI thread (the only caller of stop()) reads it.
    if (const Run* r = run_.load(std::memory_order_acquire)) {
        return r->startPosition + r->played.load(std::memory_order_relaxed);
    }
    return stoppedAt_.load(std::memory_order_relaxed);
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> Player::anchor() const
{
    const Run* r = run_.load(std::memory_order_acquire);
    if (!r || !r->anchored.load(std::memory_order_acquire)) return std::nullopt;
    return std::make_pair(r->anchorStream.load(std::memory_order_relaxed), r->startPosition);
}

std::optional<std::int64_t> Player::timelineAt(std::uint64_t streamFrame) const
{
    const auto a = anchor();
    if (!a) return std::nullopt;
    return static_cast<std::int64_t>(a->second) + (static_cast<std::int64_t>(streamFrame) -
                                                   static_cast<std::int64_t>(a->first));
}

void Player::setGain(int track, float left, float right)
{
    gains_[track] = {left, right};
    if (owned_) {
        for (auto& c : owned_->clips) {
            if (c->track != track) continue;
            c->gainL.store(left, std::memory_order_relaxed);
            c->gainR.store(right, std::memory_order_relaxed);
        }
    }
}

std::uint64_t Player::underrunFrames() const
{
    return underruns_.load(std::memory_order_relaxed);
}

std::vector<std::string> Player::takeErrors()
{
    std::lock_guard<std::mutex> lock(errorsMutex_);
    std::vector<std::string> out;
    out.swap(errors_);
    return out;
}

// Reader: one chunk per clip that has room. Silence outside the file.
void Player::fill(Run& run)
{
    for (auto& cp : run.clips) {
        Clip& c = *cp;
        const std::size_t chunk = 4096;
        if (c.ring->writeSpace() < chunk * c.channels) continue;
        std::size_t n = 0;  // frames put in the ring slots
        for (std::size_t f = 0; f < chunk;) {
            const std::int64_t t = c.produced + static_cast<std::int64_t>(f);
            const std::int64_t fileFrame = t - c.start;
            if (fileFrame < 0 || fileFrame >= static_cast<std::int64_t>(c.reader.frames()) || c.failed) {
                // Silence up to the clip start, or to the end of the chunk.
                std::size_t len = chunk - f;
                if (fileFrame < 0) len = std::min<std::size_t>(len, static_cast<std::size_t>(-fileFrame));
                for (std::size_t i = 0; i < len * c.channels; ++i) c.ring->slot(n * c.channels + i) = 0.0f;
                n += len;
                f += len;
                continue;
            }
            c.reader.seek(static_cast<std::uint64_t>(fileFrame));
            std::size_t want = std::min<std::size_t>(chunk - f, c.fileBuf.size() / c.fileChannels);
            want = std::min<std::size_t>(want, static_cast<std::size_t>(c.reader.frames() - fileFrame));
            const std::size_t got = c.reader.read(c.fileBuf.data(), want);
            for (std::size_t i = 0; i < got; ++i) {
                for (unsigned int ch = 0; ch < c.channels; ++ch) {
                    c.ring->slot((n + i) * c.channels + ch) = c.fileBuf[i * c.fileChannels + ch];
                }
            }
            if (got < want) {
                c.failed = true;  // play silence from here, report once
                if (run.errors) {
                    std::lock_guard<std::mutex> lock(*run.errorsMutex);
                    run.errors->push_back("Playback: " + c.reader.error());
                }
            }
            n += got;
            f += got;
        }
        c.ring->commitWrite(n * c.channels);
        c.produced += static_cast<std::int64_t>(n);
    }
}

void Player::readerLoop(Run* run)
{
    while (readerRun_.load(std::memory_order_relaxed)) {
        fill(*run);
        bool room = false;
        for (auto& c : run->clips) room = room || c->ring->writeSpace() >= 4096 * c->channels;
        if (!room) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Audio thread. No locks, no allocation, no I/O.
std::optional<std::uint64_t> Player::process(float* out, unsigned int frames, unsigned int outChannels,
                                             std::uint64_t blockFrame)
{
    Run* r = nullptr;
    do {
        r = run_.load(std::memory_order_seq_cst);
        inUse_.store(r, std::memory_order_seq_cst);
    } while (r != run_.load(std::memory_order_seq_cst));
    if (r == nullptr) return std::nullopt;
    const std::uint64_t blockPosition = r->startPosition + r->played.load(std::memory_order_relaxed);

    if (!r->anchored.load(std::memory_order_relaxed)) {
        r->anchorStream.store(blockFrame, std::memory_order_relaxed);
        r->anchored.store(true, std::memory_order_release);
    }
    if (out != nullptr && outChannels > 0) {
        for (unsigned int done = 0; done < frames;) {
            const unsigned int n = std::min(frames - done, kMaxBlock);
            float* o = out + static_cast<std::size_t>(done) * outChannels;
            for (auto& cp : r->clips) {
                Clip& c = *cp;
                const std::size_t ch = c.channels;
                // Pay back frames owed after an underrun first (keeps the sync).
                while (c.deficit > 0) {
                    const std::size_t avail = c.ring->readAvailable() / ch;
                    if (avail == 0) break;
                    const std::size_t drop = std::min<std::size_t>({avail, static_cast<std::size_t>(c.deficit), kMaxBlock});
                    c.ring->read(c.scratch.data(), drop * ch);
                    c.deficit -= drop;
                }
                std::size_t got = 0;
                if (c.deficit == 0) got = c.ring->read(c.scratch.data(), static_cast<std::size_t>(n) * ch) / ch;
                if (got < n) {
                    c.deficit += n - got;
                    underruns_.fetch_add(n - got, std::memory_order_relaxed);
                    for (std::size_t i = got * ch; i < static_cast<std::size_t>(n) * ch; ++i) c.scratch[i] = 0.0f;
                }
                const float l1 = c.gainL.load(std::memory_order_relaxed);
                const float r1 = c.gainR.load(std::memory_order_relaxed);
                const float l0 = c.curL, r0 = c.curR;
                if (l0 == 0.0f && l1 == 0.0f && r0 == 0.0f && r1 == 0.0f) continue;  // silent track
                const float step = 1.0f / static_cast<float>(n);
                for (unsigned int f = 0; f < n; ++f) {
                    const float t = static_cast<float>(f + 1) * step;
                    const float gl = l0 + (l1 - l0) * t;
                    const float gr = r0 + (r1 - r0) * t;
                    const float sl = c.scratch[f * ch];
                    const float sr = ch == 2 ? c.scratch[f * ch + 1] : sl;
                    if (outChannels == 1) {
                        o[f] += 0.5f * (sl * gl + sr * gr);
                    } else {
                        o[f * outChannels] += sl * gl;
                        o[f * outChannels + 1] += sr * gr;
                    }
                }
                c.curL = l1;
                c.curR = r1;
            }
            done += n;
        }
    } else {
        // No output: keep the rings moving so the timeline stays right.
        for (auto& cp : r->clips) {
            Clip& c = *cp;
            while (c.deficit > 0) {  // as with output: owed frames first (M7)
                const std::size_t avail = c.ring->readAvailable() / c.channels;
                if (avail == 0) break;
                const std::size_t drop = std::min<std::size_t>({avail, static_cast<std::size_t>(c.deficit), kMaxBlock});
                c.ring->read(c.scratch.data(), drop * c.channels);
                c.deficit -= drop;
            }
            std::size_t left = static_cast<std::size_t>(frames);
            while (left > 0) {
                const std::size_t n = std::min<std::size_t>(left, kMaxBlock);
                const std::size_t got =
                    c.deficit == 0 ? c.ring->read(c.scratch.data(), n * c.channels) / c.channels : 0;
                if (got < n) c.deficit += n - got;
                left -= n;
            }
        }
    }
    // The gain ramps also need curL/curR for silent tracks.
    for (auto& cp : r->clips) {
        cp->curL = cp->gainL.load(std::memory_order_relaxed);
        cp->curR = cp->gainR.load(std::memory_order_relaxed);
    }
    r->played.fetch_add(frames, std::memory_order_relaxed);
    inUse_.store(nullptr, std::memory_order_release);
    return blockPosition;
}

} // namespace minidaw
