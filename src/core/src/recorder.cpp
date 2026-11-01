#include "minidaw/recorder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace minidaw {

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
    if (sampleRate == 0 || inputChannels == 0) {
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
        t->channels = static_cast<unsigned int>(spec.channels.size());
        t->ring = std::make_unique<SpscRing<float>>(std::max<std::size_t>(ringFrames, 1) * t->channels);
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
    if (options.startDiskThread) {
        diskRun_.store(true);
        disk_ = std::thread(&Recorder::diskLoop, this);
    }
    active_.store(true);  // from the next audio block on, all takes record
    return true;
}

void Recorder::process(const float* input, unsigned int frames, unsigned int channels)
{
    inProcess_.fetch_add(1);
    if (active_.load() && input != nullptr && channels == inputChannels_) {
        for (auto& tp : takes_) {
            Take& t = *tp;
            const std::size_t need = static_cast<std::size_t>(frames) * t.channels;
            if (!t.ring->canWrite(need)) {
                t.dropped.fetch_add(frames, std::memory_order_relaxed);
                continue;
            }
            std::size_t k = 0;
            for (unsigned int f = 0; f < frames; ++f) {
                const float* frame = input + static_cast<std::size_t>(f) * channels;
                for (unsigned int c = 0; c < t.channels; ++c) t.ring->slot(k++) = frame[t.spec.channels[c]];
            }
            t.ring->commitWrite(need);
        }
        framesCaptured_.fetch_add(frames, std::memory_order_relaxed);
    }
    inProcess_.fetch_sub(1);
}

std::size_t Recorder::drainOnce(std::vector<float>& scratch)
{
    std::size_t moved = 0;
    for (auto& tp : takes_) {
        Take& t = *tp;
        if (!t.writer.isOpen()) continue;  // stopped after an error
        for (;;) {
            const std::size_t got = t.ring->read(scratch.data(), scratch.size() - scratch.size() % t.channels);
            if (got == 0) break;
            std::string err;
            if (!t.writer.write(scratch.data(), got / t.channels, err)) {
                setDiskError(err);
                t.writer.close(err);
                break;
            }
            moved += got / t.channels;
        }
    }
    return moved;
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
                if (!tp->writer.updateHeader(err)) setDiskError(err);
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
    for (const auto& t : takes_) n = std::max(n, t->dropped.load(std::memory_order_relaxed));
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

    // Write what is still buffered, then close the files.
    std::vector<float> scratch(16384);
    drainOnce(scratch);
    for (auto& tp : takes_) {
        Take& t = *tp;
        std::string err;
        if (!t.writer.close(err)) setDiskError(err);
        TakeResult r;
        r.path = t.spec.path;
        r.channels = t.channels;
        r.frames = t.writer.framesWritten();
        r.droppedFrames = t.dropped.load();
        r.clippedSamples = t.writer.clippedSamples();
        result.takes.push_back(r);
    }
    result.error = diskError();
    takes_.clear();
    return result;
}

} // namespace minidaw
