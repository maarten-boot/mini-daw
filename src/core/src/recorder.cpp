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
            if (instrument) {
                const bool has = sound == InstrumentSound::Sound;
                for (unsigned int f = 0; f < frames; ++f) {
                    t.ring->slot(k++) = has ? l[f] : 0.0f;
                    t.ring->slot(k++) = has ? r[f] : 0.0f;
                }
            } else {
                for (unsigned int f = 0; f < frames; ++f) {
                    const float* frame = input + static_cast<std::size_t>(f) * channels;
                    for (unsigned int c = 0; c < t.channels; ++c) t.ring->slot(k++) = frame[t.spec.channels[c]];
                }
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
