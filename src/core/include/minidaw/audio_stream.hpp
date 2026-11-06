#pragma once

// Audio device selection and the running audio stream (SPEC-DEV-02, 03, SPEC-NF-01).
//
// Opens a duplex stream on the chosen devices, measures input peak levels,
// feeds the input to the Recorder (P5) and mixes monitored inputs to the
// output (P7, SPEC-MON-01). Playback follows in P8.

#include "minidaw/config.hpp"
#include "minidaw/devices.hpp"
#include "minidaw/frame_clock.hpp"
#include "minidaw/monitor.hpp"
#include "minidaw/instrument_rack.hpp"
#include "minidaw/player.hpp"
#include "minidaw/recorder.hpp"
#include "minidaw/session_io.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class RtAudio;

namespace minidaw {

// --- Device resolution (pure logic, unit-tested) ----------------------------

enum class Direction { Input, Output };

struct ResolvedDevice {
    std::optional<unsigned int> id;   // empty = this direction is not opened
    std::string name;                 // name of the device actually used
    unsigned int channels = 0;        // channels that will be opened
    std::string warning;              // set when the wanted device was not found
};

// Find the device named `wanted` (or kDefaultDevice / kNoDevice) in `devices`.
// A missing device falls back to the system default, with a warning
// (SPEC-DEV-03, T-DEV-04).
ResolvedDevice resolveAudioDevice(const std::vector<AudioDeviceInfo>& devices, const std::string& wanted,
                                  Direction direction);

inline constexpr unsigned int kMaxInputChannels = 32;
inline constexpr unsigned int kMaxOutputChannels = 2;  // stereo master out
static_assert(kMonitorMaxChannels == kMaxInputChannels);

// Common choices offered in the settings window.
inline constexpr unsigned int kSampleRateChoices[] = {44100, 48000, 88200, 96000};
inline constexpr unsigned int kBufferSizeChoices[] = {64, 128, 256, 512, 1024, 2048};

// --- Running stream ---------------------------------------------------------

struct StreamInfo {
    bool running = false;
    std::string api;
    std::string inputName;
    std::string outputName;
    unsigned int inputChannels = 0;
    unsigned int outputChannels = 0;
    unsigned int sampleRate = 0;      // actual rate
    unsigned int bufferFrames = 0;    // actual buffer size (the driver may change it)
    double reportedLatencyMs = 0.0;   // as reported by the driver, one direction
};

struct OpenResult {
    std::string error;                // empty = stream is running
    std::vector<std::string> warnings;
};

class AudioStream : public SessionAudio {
public:
    AudioStream();
    ~AudioStream() override;
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    // Close any running stream, then open one with these settings.
    OpenResult open(const AudioSettings& settings, const DeviceScan& scan);
    void close();

    const StreamInfo& info() const { return info_; }

    // Highest absolute sample value on an input channel since the last call
    // (0 .. 1, can exceed 1 when clipping). Resets the value.
    float takeInputPeak(unsigned int channel);

    // Highest absolute output value per output channel since the last call,
    // before limiting to -1..1 (above 1.0: the output was limited). Resets it.
    float takeOutputPeak(unsigned int channel);

    // Input monitoring (SPEC-MON-01): the gains the callback mixes with from
    // its next block on. Any thread; kept when the stream is reopened.
    void setMonitor(const MonitorGains& gains);

    // Playback (SPEC-PLAY-02). Start/stop it on the GUI thread; close() stops it.
    Player& player() { return player_; }
    const Player& player() const { return player_; }
    // VST3 instruments (P10b): configured on the GUI thread, played in the callback.
    InstrumentRack& instruments() { return rack_; }

    std::uint64_t xruns() const { return xruns_.load(std::memory_order_relaxed); }
    std::uint64_t callbacks() const { return callbacks_.load(std::memory_order_relaxed); }

    // Warnings reported by the driver while running.
    std::vector<std::string> takeDriverMessages();

    // Recording of the input (SPEC-REC-03/05). Start it while the stream runs.
    // close() stops a running recording and finishes its files; the next
    // stopRecording() then returns that result (crosscheck #4, J8).
    bool startRecording(const std::vector<TakeSpec>& takes, std::string& error,
                        const RecorderOptions& options = {}) override;
    RecordingResult stopRecording() override;
    const Recorder& recorder() const override { return recorder_; }
    Recorder& recorder() override { return recorder_; }

    // SessionAudio
    bool streamRunning() const override { return info_.running; }
    unsigned int streamSampleRate() const override { return info_.sampleRate; }
    unsigned int streamInputChannels() const override { return info_.inputChannels; }

    // Audio timeline (SPEC-REC-04a): the stream frame that corresponds to a
    // steady-clock time (steadyNowNs()), once the stream runs. Any thread.
    std::optional<std::uint64_t> frameAtTime(std::int64_t ns) const override
    {
        return clock_.frameAt(ns, clockRate_.load(std::memory_order_acquire));
    }
    std::optional<std::uint64_t> currentFrame() const override { return frameAtTime(steadyNowNs()); }

private:
    static int audioCallback(void* output, void* input, unsigned int frames, double streamTime,
                             unsigned int status, void* user);

    std::unique_ptr<RtAudio> rtaudio_;
    StreamInfo info_;
    Recorder recorder_;
    std::optional<RecordingResult> closedResult_;  // a recording ended by close()

    // Read by the audio thread; only changed while the stream is closed.
    unsigned int cbInputChannels_ = 0;
    unsigned int cbOutputChannels_ = 0;

    // Shared with the audio thread (lock-free).
    std::array<std::atomic<float>, kMaxInputChannels> peaks_{};
    std::array<std::atomic<float>, kMaxOutputChannels> outPeaks_{};
    std::array<std::atomic<float>, kMaxInputChannels> monitorLeft_{};
    std::array<std::atomic<float>, kMaxInputChannels> monitorRight_{};
    MonitorGains monitorCurrent_;  // audio thread only: gains of the last block
    Player player_;
    InstrumentRack rack_;
    std::atomic<std::uint64_t> xruns_{0};
    std::atomic<std::uint64_t> callbacks_{0};
    FrameClock clock_;
    std::atomic<unsigned int> clockRate_{0};  // 0 while no stream runs
    std::uint64_t framePos_ = 0;   // audio thread only: frames received since open
    ClockSmoother smoother_;       // audio thread only
    unsigned int cbSampleRate_ = 0;    // read by the audio thread; set before the stream starts
    unsigned int cbBufferFrames_ = 0;
    unsigned int smootherRate_ = 0;    // audio thread only: rate the smoother was started with

    // Driver error messages (not from the audio callback itself).
    std::mutex messagesMutex_;
    std::vector<std::string> messages_;
};

// Peak value -> dBFS text, e.g. "-12.3 dBFS" or "-inf dBFS".
std::string formatDbfs(float peak);

} // namespace minidaw
