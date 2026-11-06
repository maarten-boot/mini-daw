#include "minidaw/recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace minidaw {

namespace {
constexpr std::size_t kGapSlots = 256;           // gap markers waiting for the disk thread
constexpr std::uint64_t kSilenceChunk = 8192;    // frames of silence per write
} // namespace

bool RecordingResult::lostData() const
{
    if (!error.empty()) return true;
    return std::any_of(takes.begin(), takes.end(), [](const TakeResult& t) { return t.droppedFrames > 0; });
}

Recorder::~Recorder()
{
    if (active_.load() || disk_.joinable() || !takes_.empty()) stop();
}

bool Recorder::start(const std::vector<TakeSpec>& takes, unsigned int sampleRate, unsigned int inputChannels,
                     std::string& error, const RecorderOptions& options)
{
    if (active_.load() || disk_.joinable() || !takes_.empty()) stop();
    error.clear();
    if (takes.empty()) {
        error = "Nothing to record: no takes were given.";
        return false;
    }
    const bool needsInput = std::any_of(takes.begin(), takes.end(), [](const TakeSpec& t) { return t.instrumentTrack <= 0; });
    if (sampleRate == 0 || (needsInput && inputChannels == 0)) {
        error = "Nothing to record: the audio stream has no input.";
        return false;
    }

    options_ = options;
    sampleRate_ = sampleRate;
    inputChannels_ = inputChannels;
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        diskError_.clear();
    }

    const auto ringFrames = static_cast<std::size_t>(std::ceil(options.ringSeconds * sampleRate));
    // Check everything before creating any file.
    for (const auto& spec : takes) {
        if (spec.instrumentTrack > 0) continue;  // stereo instrument sound
        if (spec.channels.empty() || spec.channels.size() > 2) {
            error = "A take must record 1 or 2 channels.";
            return false;
        }
        for (unsigned int ch : spec.channels) {
            if (ch >= inputChannels) {
                error = "Input channel " + std::to_string(ch + 1) + " does not exist (the input has " +
                        std::to_string(inputChannels) + ").";
                return false;
            }
        }
    }

    std::vector<std::unique_ptr<Take>> made;
    for (const auto& spec : takes) {
        auto t = std::make_unique<Take>();
        t->spec = spec;
        t->channels = spec.instrumentTrack > 0 ? 2u : static_cast<unsigned int>(spec.channels.size());
        t->ring = std::make_unique<SpscRing<float>>(std::max<std::size_t>(ringFrames, 1) * t->channels);
        t->gaps = std::make_unique<SpscRing<Gap>>(kGapSlots);
        if (options.parts) {  // files are opened per part (beginPart)
            t->historyFrames = static_cast<std::uint64_t>(std::ceil(std::max(options.partBufferSeconds, 0.1) * sampleRate));
            t->history.assign(static_cast<std::size_t>(t->historyFrames) * t->channels, 0.0f);
            made.push_back(std::move(t));
            continue;
        }
        if (!t->writer.open(spec.path, sampleRate, t->channels, error)) {
            // Remove files already created for this attempt.
            for (auto& m : made) {
                std::string ignored;
                m->writer.close(ignored);
                std::error_code ec;
                std::filesystem::remove(m->spec.path, ec);
            }
            return false;
        }
        made.push_back(std::move(t));
    }

    takes_ = std::move(made);
    {
        std::lock_guard<std::mutex> lock(partMutex_);
        incoming_.assign(takes_.size(), {});
        finished_.clear();
    }
    queued_.assign(takes_.size(), {});
    framesCaptured_.store(0);
    haveStart_.store(false);
    if (options.startDiskThread) {
        diskRun_.store(true);
        disk_ = std::thread(&Recorder::diskLoop, this);
    }
    active_.store(true);  // from the next audio block on, all takes record
    return true;
}

void Recorder::process(const float* input, unsigned int frames, unsigned int channels, std::uint64_t blockFrame,
                       const InstrumentSource* instruments)
{
    inProcess_.fetch_add(1);
    // With input takes, a block without the expected input is skipped for all
    // takes (they stay in step); a recording of instruments only needs no input.
    const bool inputOk = input != nullptr && channels == inputChannels_ && channels > 0;
    if (active_.load() && frames > 0 && (inputChannels_ == 0 || inputOk)) {
        if (!haveStart_.load(std::memory_order_relaxed)) {
            startFrame_.store(blockFrame, std::memory_order_relaxed);
            haveStart_.store(true, std::memory_order_release);
        }
        for (auto& tp : takes_) {
            Take& t = *tp;
            const std::size_t need = static_cast<std::size_t>(frames) * t.channels;
            const bool instrument = t.spec.instrumentTrack > 0;
            // After a loss, the gap must be announced before the next block, so
            // the disk thread can put the silence in the right place.
            const float* l = nullptr;
            const float* r = nullptr;
            const InstrumentSound sound = instrument && instruments != nullptr
                                              ? instruments->instrumentOutput(t.spec.instrumentTrack, frames, l, r)
                                              : InstrumentSound::None;
            // An instrument block that could not be kept is a loss like a full ring (R7).
            const bool fits = t.ring->canWrite(need) && (t.openGap == 0 || t.gaps->canWrite(1)) &&
                              sound != InstrumentSound::Lost;
            if (!fits) {
                t.openGap += frames;
                t.dropped.fetch_add(frames, std::memory_order_relaxed);
                continue;
            }
            if (t.openGap != 0) {
                const Gap g{t.queued, t.openGap};
                t.gaps->write(&g, 1);
                t.openGap = 0;
            }
            std::size_t k = 0;
            float peak = 0.0f;
            if (instrument) {
                const bool has = sound == InstrumentSound::Sound;
                for (unsigned int f = 0; f < frames; ++f) {
                    const float a = has ? l[f] : 0.0f;
                    const float b = has ? r[f] : 0.0f;
                    t.ring->slot(k++) = a;
                    t.ring->slot(k++) = b;
                    peak = std::max(peak, std::max(std::fabs(a), std::fabs(b)));
                }
            } else {
                for (unsigned int f = 0; f < frames; ++f) {
                    const float* frame = input + static_cast<std::size_t>(f) * channels;
                    for (unsigned int c = 0; c < t.channels; ++c) {
                        const float v = frame[t.spec.channels[c]];
                        t.ring->slot(k++) = v;
                        peak = std::max(peak, std::fabs(v));
                    }
                }
            }
            if (options_.parts && peak >= options_.activityLevel) {
                t.activeUntil.store(blockFrame + frames, std::memory_order_relaxed);
            }
            t.ring->commitWrite(need);
            t.queued += frames;
        }
        framesCaptured_.fetch_add(frames, std::memory_order_relaxed);
    }
    inProcess_.fetch_sub(1);
}

bool Recorder::writeSilence(Take& t, std::uint64_t frames)
{
    while (frames > 0) {
        const std::uint64_t n = std::min(frames, kSilenceChunk);
        std::string err;
        if (!t.writer.write(nullptr, static_cast<std::size_t>(n), err)) {
            setDiskError(err);
            t.writer.close(err);
            return false;
        }
        t.silenceWritten += n;
        frames -= n;
    }
    return true;
}

std::size_t Recorder::drainOnce(std::vector<float>& scratch)
{
    if (options_.parts) return drainParts(scratch);
    std::size_t moved = 0;
    for (auto& tp : takes_) {
        Take& t = *tp;
        while (t.writer.isOpen()) {  // closed after an error: the ring fills up and losses are counted
            if (!t.hasPendingGap) t.hasPendingGap = t.gaps->read(&t.pendingGap, 1) == 1;
            if (t.hasPendingGap && t.pendingGap.at == t.ringRead) {
                t.hasPendingGap = false;
                if (!writeSilence(t, t.pendingGap.length)) break;
                moved += static_cast<std::size_t>(t.pendingGap.length);
                continue;
            }
            std::size_t maxSamples = scratch.size() - scratch.size() % t.channels;
            if (t.hasPendingGap) {
                const std::uint64_t untilGap = (t.pendingGap.at - t.ringRead) * t.channels;
                maxSamples = static_cast<std::size_t>(std::min<std::uint64_t>(maxSamples, untilGap));
            }
            const std::size_t got = t.ring->read(scratch.data(), maxSamples);
            if (got == 0) break;
            const std::size_t frames = got / t.channels;
            t.ringRead += frames;
            std::string err;
            if (!t.writer.write(scratch.data(), frames, err)) {
                t.lostOnDisk.fetch_add(frames, std::memory_order_relaxed);
                setDiskError(err);
                t.writer.close(err);
                break;
            }
            moved += frames;
        }
    }
    return moved;
}

// --- Parts mode (P16) ---------------------------------------------------------

void Recorder::beginPart(int id, std::uint64_t fromFrame, const std::vector<std::filesystem::path>& paths)
{
    std::lock_guard<std::mutex> lock(partMutex_);
    for (std::size_t i = 0; i < incoming_.size(); ++i) {
        PartCommand c;
        c.begin = true;
        c.id = id;
        c.frame = fromFrame;
        if (i < paths.size()) c.path = paths[i];
        incoming_[i].push_back(c);
    }
}

void Recorder::endPart(std::uint64_t atFrame)
{
    std::lock_guard<std::mutex> lock(partMutex_);
    for (auto& q : incoming_) {
        PartCommand c;
        c.frame = atFrame;
        q.push_back(c);
    }
}

std::vector<PartTakeResult> Recorder::takeFinishedParts()
{
    std::lock_guard<std::mutex> lock(partMutex_);
    std::vector<PartTakeResult> out;
    out.swap(finished_);
    return out;
}

std::uint64_t Recorder::lastActiveFrame() const
{
    std::uint64_t f = 0;
    for (const auto& t : takes_) f = std::max(f, t->activeUntil.load(std::memory_order_relaxed));
    return f;
}

void Recorder::passFrames(Take& t, std::size_t /*index*/, const float* data, std::uint64_t frames)
{
    const unsigned int ch = t.channels;
    if (t.historyFrames > 0) {  // remembered for a part that starts a little in the past
        for (std::uint64_t k = 0; k < frames; ++k) {
            float* dst = &t.history[static_cast<std::size_t>(((t.pos + k) % t.historyFrames) * ch)];
            for (unsigned int c = 0; c < ch; ++c) dst[c] = data ? data[k * ch + c] : 0.0f;
        }
    }
    if (t.writing && t.writer.isOpen()) {
        const std::uint64_t lo = std::max(t.pos, t.partStart);
        const std::uint64_t hi = std::min(t.pos + frames, t.partEnd);
        if (hi > lo) {
            std::string err;
            if (!t.writer.write(data ? data + (lo - t.pos) * ch : nullptr, static_cast<std::size_t>(hi - lo), err)) {
                setDiskError(err);
                t.partError = err;
                std::string ignored;
                t.writer.close(ignored);
            } else if (data == nullptr) {
                t.partLost += hi - lo;
            }
        }
    }
    t.pos += frames;
}

void Recorder::startPart(Take& t, const PartCommand& c)
{
    t.writing = true;
    t.partId = c.id;
    t.partEnd = UINT64_MAX;
    t.partLost = 0;
    t.partError.clear();
    const std::uint64_t oldest = t.pos > t.historyFrames ? t.pos - t.historyFrames : 0;
    t.partStart = std::max(c.frame, oldest);
    t.spec.path = c.path;
    std::string err;
    if (!t.writer.open(c.path, sampleRate_, t.channels, err)) {
        t.partError = err;
        setDiskError(err);
        return;
    }
    // The frames from the part's start up to now come from memory.
    const unsigned int ch = t.channels;
    for (std::uint64_t f = t.partStart; f < t.pos;) {
        const std::uint64_t idx = f % t.historyFrames;
        const std::uint64_t n = std::min(t.pos - f, t.historyFrames - idx);
        if (!t.writer.write(&t.history[static_cast<std::size_t>(idx * ch)], static_cast<std::size_t>(n), err)) {
            setDiskError(err);
            t.partError = err;
            std::string ignored;
            t.writer.close(ignored);
            return;
        }
        f += n;
    }
}

void Recorder::finishPart(Take& t)
{
    const std::uint64_t end = std::min(t.partEnd, t.pos);
    const std::uint64_t keep = end > t.partStart ? end - t.partStart : 0;
    PartTakeResult r;
    r.part = t.partId;
    r.path = t.spec.path;
    r.lostFrames = t.partLost;
    r.error = t.partError;
    if (t.writer.isOpen()) {
        t.writer.keepOnlyFrames(keep);  // what was written after the end is cut off
        std::string err;
        if (!t.writer.close(err)) {
            setDiskError(err);
            if (r.error.empty()) r.error = err;
        }
        r.frames = t.writer.framesWritten();
        r.clippedSamples = t.writer.clippedSamples();
    }
    t.writing = false;
    t.partEnd = UINT64_MAX;
    std::lock_guard<std::mutex> lock(partMutex_);
    finished_.push_back(r);
}

std::size_t Recorder::drainParts(std::vector<float>& scratch)
{
    {
        std::lock_guard<std::mutex> lock(partMutex_);
        for (std::size_t i = 0; i < incoming_.size() && i < queued_.size(); ++i) {
            queued_[i].insert(queued_[i].end(), incoming_[i].begin(), incoming_[i].end());
            incoming_[i].clear();
        }
    }
    std::size_t moved = 0;
    for (std::size_t i = 0; i < takes_.size(); ++i) {
        Take& t = *takes_[i];
        auto& q = queued_[i];
        for (;;) {
            // Commands that can be carried out now (in order).
            while (!q.empty()) {
                const PartCommand& c = q.front();
                if (c.begin) {
                    if (t.writing) finishPart(t);  // a begin without an end: end the old part here
                    startPart(t, c);
                } else if (t.writing) {
                    t.partEnd = std::max(c.frame, t.partStart);
                    if (t.pos < t.partEnd) break;  // not there yet
                    finishPart(t);
                }
                q.erase(q.begin());
            }
            // No further than the end of the current part, so the next part starts cleanly.
            std::uint64_t limit = UINT64_MAX;
            if (t.writing && t.partEnd != UINT64_MAX) limit = t.partEnd - t.pos;
            if (!t.hasPendingGap) t.hasPendingGap = t.gaps->read(&t.pendingGap, 1) == 1;
            if (t.hasPendingGap && t.pendingGap.at == t.ringRead) {
                const std::uint64_t n = std::min({t.pendingGap.length, limit, kSilenceChunk});
                passFrames(t, i, nullptr, n);
                t.pendingGap.length -= n;
                if (t.pendingGap.length == 0) t.hasPendingGap = false;
                moved += static_cast<std::size_t>(n);
                continue;
            }
            std::size_t maxSamples = scratch.size() - scratch.size() % t.channels;
            if (t.hasPendingGap) {
                const std::uint64_t untilGap = (t.pendingGap.at - t.ringRead) * t.channels;
                maxSamples = static_cast<std::size_t>(std::min<std::uint64_t>(maxSamples, untilGap));
            }
            if (limit != UINT64_MAX) {
                maxSamples = static_cast<std::size_t>(std::min<std::uint64_t>(maxSamples, limit * t.channels));
            }
            const std::size_t got = t.ring->read(scratch.data(), maxSamples);
            if (got == 0) break;
            const std::size_t frames = got / t.channels;
            t.ringRead += frames;
            passFrames(t, i, scratch.data(), frames);
            moved += frames;
        }
    }
    return moved;
}

void Recorder::drainForTest()
{
    if (!disk_.joinable()) {
        std::vector<float> scratch(16384);
        drainOnce(scratch);
    }
}

void Recorder::diskLoop()
{
    std::vector<float> scratch(16384);
    auto lastHeader = std::chrono::steady_clock::now();
    const auto headerEvery = std::chrono::duration<double>(options_.headerUpdateSeconds);
    while (diskRun_.load()) {
        const std::size_t moved = drainOnce(scratch);
        const auto now = std::chrono::steady_clock::now();
        if (now - lastHeader >= headerEvery) {
            lastHeader = now;
            for (auto& tp : takes_) {
                std::string err;
                if (!tp->writer.updateHeader(err, options_.syncToDisk)) setDiskError(err);
            }
        }
        if (moved == 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void Recorder::setDiskError(const std::string& text)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    if (diskError_.empty()) diskError_ = text;
}

std::uint64_t Recorder::droppedFrames() const
{
    std::uint64_t n = 0;
    for (const auto& t : takes_) {
        n = std::max(n, t->dropped.load(std::memory_order_relaxed) + t->lostOnDisk.load(std::memory_order_relaxed));
    }
    return n;
}

std::string Recorder::diskError() const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    return diskError_;
}

RecordingResult Recorder::stop()
{
    RecordingResult result;
    active_.store(false);
    while (inProcess_.load() != 0) std::this_thread::yield();  // audio thread leaves process()

    diskRun_.store(false);
    if (disk_.joinable()) disk_.join();

    // Write what is still buffered, end every take with the silence of a loss
    // that was still open, so all takes have the same length; close the files.
    std::vector<float> scratch(16384);
    drainOnce(scratch);
    if (options_.parts) {
        // Parts mode: carry out what is left; a part still open ends here.
        for (std::size_t i = 0; i < takes_.size(); ++i) {
            Take& t = *takes_[i];
            for (std::uint64_t left = t.openGap; left > 0;) {
                const std::uint64_t n = std::min(left, kSilenceChunk);
                passFrames(t, i, nullptr, n);
                left -= n;
            }
            for (const PartCommand& c : queued_[i]) {
                if (c.begin) {
                    if (t.writing) finishPart(t);
                    startPart(t, c);
                } else if (t.writing) {
                    t.partEnd = std::max(c.frame, t.partStart);
                    finishPart(t);
                }
            }
            queued_[i].clear();
            if (t.writing) finishPart(t);
        }
        {
            std::lock_guard<std::mutex> lock(partMutex_);
            for (std::size_t i = 0; i < incoming_.size(); ++i) {
                Take& t = *takes_[i];
                for (const PartCommand& c : incoming_[i]) {
                    if (c.begin) {
                        // Too late to start: report it empty.
                        PartTakeResult r;
                        r.part = c.id;
                        r.path = c.path;
                        finished_.push_back(r);
                    }
                }
                incoming_[i].clear();
                (void)t;
            }
        }
        result.error = diskError();
        takes_.clear();
        return result;
    }
    const std::uint64_t captured = framesCaptured_.load();
    for (auto& tp : takes_) {
        Take& t = *tp;
        if (t.writer.isOpen() && t.openGap > 0) writeSilence(t, t.openGap);
        std::string err;
        if (!t.writer.close(err)) setDiskError(err);
        TakeResult r;
        r.path = t.spec.path;
        r.channels = t.channels;
        r.frames = t.writer.framesWritten();
        // Everything captured that is not real audio in the file was lost,
        // whatever the reason (ring full, disk error, left in the ring).
        const std::uint64_t audioInFile = r.frames - t.silenceWritten;
        r.droppedFrames = captured > audioInFile ? captured - audioInFile : 0;
        r.clippedSamples = t.writer.clippedSamples();
        result.takes.push_back(r);
    }
    result.error = diskError();
    takes_.clear();
    return result;
}

} // namespace minidaw
