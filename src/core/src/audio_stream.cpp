#include "minidaw/audio_stream.hpp"

#include "console_capture.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

namespace minidaw {

// ---------------------------------------------------------------------------
// Device resolution
// ---------------------------------------------------------------------------
namespace {

unsigned int channelsFor(const AudioDeviceInfo& d, Direction dir)
{
    return dir == Direction::Input ? d.inputChannels : d.outputChannels;
}

bool isDefaultFor(const AudioDeviceInfo& d, Direction dir)
{
    return dir == Direction::Input ? d.isDefaultInput : d.isDefaultOutput;
}

ResolvedDevice use(const AudioDeviceInfo& d, Direction dir)
{
    const unsigned int maxCh = dir == Direction::Input ? kMaxInputChannels : kMaxOutputChannels;
    return {d.id, d.name, std::min(channelsFor(d, dir), maxCh), {}};
}

} // namespace

ResolvedDevice resolveAudioDevice(const std::vector<AudioDeviceInfo>& devices, const std::string& wanted,
                                  Direction dir)
{
    const char* what = dir == Direction::Input ? "Input" : "Output";
    if (wanted == kNoDevice) {
        return {};
    }

    const AudioDeviceInfo* fallback = nullptr;
    for (const auto& d : devices) {
        if (channelsFor(d, dir) == 0) continue;
        if (isDefaultFor(d, dir)) { fallback = &d; break; }
        if (!fallback) fallback = &d;  // first usable device if none is marked default
    }

    if (wanted != kDefaultDevice && !wanted.empty()) {
        for (const auto& d : devices) {
            if (d.name == wanted && channelsFor(d, dir) > 0) return use(d, dir);
        }
        if (fallback) {
            ResolvedDevice r = use(*fallback, dir);
            r.warning = std::string(what) + " device \"" + wanted + "\" was not found; using \"" + fallback->name +
                        "\" instead.";
            return r;
        }
        ResolvedDevice r;
        r.warning = std::string(what) + " device \"" + wanted + "\" was not found, and there is no other " +
                    (dir == Direction::Input ? "input" : "output") + " device.";
        return r;
    }

    if (fallback) return use(*fallback, dir);
    ResolvedDevice r;
    r.warning = std::string("There is no ") + (dir == Direction::Input ? "input" : "output") + " device.";
    return r;
}

std::string formatDbfs(float peak)
{
    if (!(peak > 0.0f)) return "-inf dBFS";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f dBFS", 20.0 * std::log10(static_cast<double>(peak)));
    return buf;
}

// ---------------------------------------------------------------------------
// AudioStream
// ---------------------------------------------------------------------------
// RtAudio is created on first use (open), so `mini-daw --version` and other
// commands that never open a stream do not search for audio devices.
AudioStream::AudioStream() = default;

AudioStream::~AudioStream()
{
    close();
}

void AudioStream::close()
{
    if (rtaudio_ && rtaudio_->isStreamOpen()) {
        if (rtaudio_->isStreamRunning()) rtaudio_->stopStream();
        rtaudio_->closeStream();
    }
    info_ = {};
    cbInputChannels_ = 0;
    cbOutputChannels_ = 0;
}

OpenResult AudioStream::open(const AudioSettings& settings, const DeviceScan& scan)
{
    close();
    OpenResult result;

    if (!rtaudio_) {
        detail::ConsoleCapture capture;  // the constructor may print before the callback is attached
        rtaudio_ = std::make_unique<RtAudio>(RtAudio::UNSPECIFIED, [this](RtAudioErrorType type, const std::string& text) {
            if (type == RTAUDIO_NO_ERROR) return;
            std::lock_guard<std::mutex> lock(messagesMutex_);
            messages_.push_back(text);
        });
        rtaudio_->showWarnings(false);  // warnings go to the callback above, not to the console
        detail::appendCaptured(capture, "", result.warnings);
    }

    const ResolvedDevice in = resolveAudioDevice(scan.audioDevices, settings.inputDevice, Direction::Input);
    const ResolvedDevice out = resolveAudioDevice(scan.audioDevices, settings.outputDevice, Direction::Output);
    if (!in.warning.empty()) result.warnings.push_back(in.warning);
    if (!out.warning.empty()) result.warnings.push_back(out.warning);
    if (!in.id && !out.id) {
        result.error = "No input or output device is selected, so no audio stream was opened.";
        return result;
    }

    RtAudio::StreamParameters inParams;
    RtAudio::StreamParameters outParams;
    if (in.id) {
        inParams.deviceId = *in.id;
        inParams.nChannels = in.channels;
        inParams.firstChannel = 0;
    }
    if (out.id) {
        outParams.deviceId = *out.id;
        outParams.nChannels = out.channels;
        outParams.firstChannel = 0;
    }

    RtAudio::StreamOptions options;
    options.streamName = "mini-daw";
    options.flags = RTAUDIO_SCHEDULE_REALTIME;

    for (auto& p : peaks_) p.store(0.0f, std::memory_order_relaxed);
    xruns_.store(0, std::memory_order_relaxed);
    callbacks_.store(0, std::memory_order_relaxed);
    cbInputChannels_ = in.id ? in.channels : 0;
    cbOutputChannels_ = out.id ? out.channels : 0;

    unsigned int frames = settings.bufferFrames;
    RtAudioErrorType err = rtaudio_->openStream(out.id ? &outParams : nullptr, in.id ? &inParams : nullptr,
                                                RTAUDIO_FLOAT32, settings.sampleRate, &frames,
                                                &AudioStream::audioCallback, this, &options);
    if (err != RTAUDIO_NO_ERROR) {
        result.error = "The audio stream could not be opened: " + rtaudio_->getErrorText();
        close();
        return result;
    }
    err = rtaudio_->startStream();
    if (err != RTAUDIO_NO_ERROR) {
        result.error = "The audio stream could not be started: " + rtaudio_->getErrorText();
        close();
        return result;
    }

    // Some drivers report success from startStream() and only fail on their
    // own thread afterwards (seen with WASAPI under Wine). Wait for the first
    // callback so "running" really means audio is flowing.
    for (int i = 0; i < 100 && callbacks_.load(std::memory_order_relaxed) == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (callbacks_.load(std::memory_order_relaxed) == 0) {
        std::vector<std::string> msgs = takeDriverMessages();
        result.error = "The audio stream started but no audio arrived within 1 second.";
        for (const auto& m : msgs) result.error += " Driver: " + m;
        close();
        return result;
    }

    info_.running = true;
    info_.api = RtAudio::getApiDisplayName(rtaudio_->getCurrentApi());
    info_.inputName = in.id ? in.name : std::string();
    info_.outputName = out.id ? out.name : std::string();
    info_.inputChannels = cbInputChannels_;
    info_.outputChannels = cbOutputChannels_;
    info_.sampleRate = rtaudio_->getStreamSampleRate();
    info_.bufferFrames = frames;
    if (info_.sampleRate > 0) {
        info_.reportedLatencyMs = 1000.0 * static_cast<double>(rtaudio_->getStreamLatency()) / info_.sampleRate;
    }
    if (info_.sampleRate != settings.sampleRate) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "The device runs at %u Hz instead of the requested %u Hz.", info_.sampleRate,
                      settings.sampleRate);
        result.warnings.emplace_back(buf);
    }
    return result;
}

float AudioStream::takeInputPeak(unsigned int channel)
{
    if (channel >= kMaxInputChannels) return 0.0f;
    return peaks_[channel].exchange(0.0f, std::memory_order_relaxed);
}

std::vector<std::string> AudioStream::takeDriverMessages()
{
    std::lock_guard<std::mutex> lock(messagesMutex_);
    std::vector<std::string> out;
    out.swap(messages_);
    return out;
}

// Runs on the audio thread: no locks, no allocation, no I/O (SPEC-NF-01).
int AudioStream::audioCallback(void* output, void* input, unsigned int frames, double /*streamTime*/,
                               unsigned int status, void* user)
{
    auto* self = static_cast<AudioStream*>(user);
    self->callbacks_.fetch_add(1, std::memory_order_relaxed);
    if (status != 0) {
        self->xruns_.fetch_add(1, std::memory_order_relaxed);
    }

    if (output != nullptr) {
        std::memset(output, 0, sizeof(float) * frames * self->cbOutputChannels_);
    }

    if (input != nullptr) {
        const auto* samples = static_cast<const float*>(input);  // interleaved
        const unsigned int channels = self->cbInputChannels_;
        for (unsigned int ch = 0; ch < channels; ++ch) {
            float peak = 0.0f;
            for (unsigned int f = 0; f < frames; ++f) {
                peak = std::max(peak, std::fabs(samples[f * channels + ch]));
            }
            auto& slot = self->peaks_[ch];
            float prev = slot.load(std::memory_order_relaxed);
            while (peak > prev && !slot.compare_exchange_weak(prev, peak, std::memory_order_relaxed)) {
            }
        }
    }
    return 0;
}

} // namespace minidaw
