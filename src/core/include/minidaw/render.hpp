#pragma once

// Rendering a MIDI take through its instrument to a WAV file, faster than
// real time (P11, SPEC-VST-04a).
//
// start() (GUI thread) loads a second instance of the plugin for offline
// processing, gives it the track's settings and starts a worker thread that
// plays the take's events at their exact frames and writes the stereo sound
// (24-bit, the session rate) from the take's frame 0. After the take's end
// the sound is kept until it has been silent (below -80 dBFS) for 0.5 s, at
// most `maxTailSeconds`. The audio stream and the live instrument are not
// touched. finish() (GUI thread, after done()) ends the thread and unloads
// the second instance. A cancelled or failed render removes its file.

#include "minidaw/instrument.hpp"
#include "minidaw/smf.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

struct RenderJob {
    std::filesystem::path module;
    std::string classId;
    std::string state, controllerState;   // the plugin's settings (as saved in the project)
    unsigned int sampleRate = 48000;
    std::vector<MidiEvent> events;        // the take's events, frames from the take's start
    std::uint64_t takeFrames = 0;         // the take's length (its end of track)
    std::filesystem::path out;            // WAV file to write (replaced)
    double maxTailSeconds = 10.0;
};

struct RenderResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::uint64_t frames = 0;             // frames written
    float peak = 0.0f;                    // highest sample (to spot overloads)
};

class OfflineRenderer {
public:
    static constexpr unsigned int kBlock = 512;

    OfflineRenderer() = default;
    ~OfflineRenderer();
    OfflineRenderer(const OfflineRenderer&) = delete;
    OfflineRenderer& operator=(const OfflineRenderer&) = delete;

    bool start(RenderJob job, std::string& error);
    bool active() const { return worker_.joinable(); }   // started and not finished
    bool done() const { return done_.load(std::memory_order_acquire); }
    // 0..1 of the take (the tail is not counted).
    double progress() const;
    void cancel() { cancel_.store(true); }
    RenderResult finish();

private:
    void run();

    RenderJob job_;
    Vst3Instrument instrument_;
    std::thread worker_;
    std::atomic<bool> done_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<std::uint64_t> position_{0};
    RenderResult result_;                 // the worker's; read after join
};

} // namespace minidaw
