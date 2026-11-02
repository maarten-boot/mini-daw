// mini-daw — application entry point.
//
// Command line:
//   mini-daw                       open the GUI
//   mini-daw --config <file>       use this config file instead of the default one
//   mini-daw --list-devices        print audio devices and MIDI ports, then exit
//   mini-daw --audio-test <sec>    open the configured audio devices for <sec> seconds,
//                                  print the actual settings and input levels, then exit
//   mini-daw --record-test <sec> <folder>
//                                  record input channel 1 (and 2, if present) and every
//                                  MIDI input for <sec> seconds into a take folder in
//                                  <folder>, then exit
//   mini-daw --contrast-report     print every colour pair and its contrast, then exit
//   mini-daw --version             print the version, then exit

#include "imgui_theme.hpp"
#include "renderer.hpp"

#include "minidaw/audio_stream.hpp"
#include "minidaw/config.hpp"
#include "minidaw/devices.hpp"
#include "minidaw/log.hpp"
#include "minidaw/meter.hpp"
#include "minidaw/midi_input.hpp"
#include "minidaw/midi_player.hpp"
#include "minidaw/monitor.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/project.hpp"
#include "minidaw/recording_session.hpp"
#include "minidaw/track.hpp"
#include "minidaw/theme.hpp"
#include "minidaw/version.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

using minidaw::ContrastPreset;
using minidaw::ContrastSetting;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct AppState {
    std::filesystem::path configPath;
    minidaw::AppConfig config;
    std::vector<std::string> configWarnings;
    std::string saveError;

    // Audio stream watchdog (P8): callbacks that stop arriving.
    std::uint64_t lastCallbacks = 0;
    double callbacksChangedAt = 0.0;
    bool streamStalled = false;

    // Diagnostics log (SPEC-LOG-01).
    minidaw::Log log;
    bool showLog = false;
    bool logOnlyProblems = false;
    std::uint64_t logLinesShown = 0;
    std::string logError;

    minidaw::Palette palette;                    // after enforcement
    std::vector<minidaw::ColorPair> unresolved;  // should stay empty

    bool showCustomContrast = false;
    bool showContrastReport = false;
    double customInput = 7.0;
    std::string customError;

    minidaw::DeviceScan scan;
    std::string rendererName;

    // Audio (P4)
    minidaw::AudioStream audio;
    std::vector<std::string> audioWarnings;
    std::string audioError;
    bool showAudioSettings = false;
    minidaw::AudioSettings pendingAudio;         // edited in the settings window
    // Meters (P7, SPEC-MON-02): per input channel and per output channel.
    std::array<minidaw::LevelMeter, minidaw::kMaxInputChannels> inputMeters{};
    std::array<minidaw::LevelMeter, minidaw::kMaxOutputChannels> outputMeters{};
    minidaw::MonitorGains monitorSent;     // last gains handed to the audio stream (SPEC-MON-01)
    struct MidiActivity {
        std::uint64_t count = 0;
        double changedAt = -10.0;
    };
    std::map<std::string, MidiActivity> midiActivity;  // per MIDI input port

    // Tracks and recording (P6). Declared after `audio`, so they are destroyed first.
    std::vector<minidaw::Track> tracks;
    bool tracksCreated = false;
    minidaw::MidiInputs midi;
    bool midiPortsDirty = true;            // armed MIDI tracks changed: reopen ports
    double midiCheckAt = 0.0;              // next MIDI port check (hot-plug, SPEC-REC-01b)
    std::vector<std::string> midiWarnings; // the latest MIDI events, shown under the tracks
    minidaw::RecordingSession session;
    std::string recordError;
    std::string loggedRecordingError;      // the disk error already written to the log
    std::vector<std::string> lastTake;     // summary of the last recording
    bool showRecordingSettings = false;
    minidaw::RecordingSettings pendingRecording;

    // Project (P9, SPEC-PRJ-01/02): empty folder = no project open.
    std::filesystem::path projectFolder;
    std::string projectSavedText;          // the JSON last written (autosave writes only changes)
    double projectCheckAt = 0.0;
    std::string projectError;              // last save error (shown, logged once)
    std::vector<std::string> projectNotes; // warnings from opening / copying takes
    enum class ProjectDialog { None, New, Open, SaveAs } projectDialog = ProjectDialog::None;
    std::string projectNameInput;
    std::string projectFolderInput;
    std::string projectDialogError;
    std::string windowTitle;               // shown in the title bar

    // Playback (P8). Declared last: destroyed first (its thread reads `audio`).
    minidaw::MidiPlayer midiPlayer;
    bool playbackForRecording = false;     // the run was started by Record (timeline for the takes)
    bool overdub = false;                  // takes were playing while recording: shift the new ones
    std::uint64_t recordStartPosition = 0; // play position when Record was pressed (placement fallback, M6)
    std::int64_t midiDelayFrames = 0;      // MIDI output delay of the current run (end check, M3)
    std::vector<std::string> playWarnings;
    bool showTakeBrowser = false;
    std::string takeFolderInput;
    std::vector<std::filesystem::path> takeFolders;
    std::string takeBrowserError;
};

void saveConfig(AppState& app)
{
    app.saveError = minidaw::saveConfigFile(app.configPath, app.config);
    if (!app.saveError.empty()) app.log.error("Could not save the config file: " + app.saveError);
}

// Start the log for this run, next to the config file in use (SPEC-LOG-01).
void openLog(AppState& app, const char* mode)
{
    const std::filesystem::path file = app.configPath.parent_path() / "mini-daw.log";
    app.logError = app.log.open(file);
    app.log.info(std::string(minidaw::kProductName) + " " + minidaw::kVersion + " started (" + mode + ")");
    app.log.info("Config file: " + minidaw::pathToUtf8(app.configPath));
    if (!app.logError.empty()) std::fprintf(stderr, "warning: %s\n", app.logError.c_str());
    for (const auto& w : app.configWarnings) app.log.warning("Config: " + w);
}

void logScan(AppState& app)
{
    app.log.info("Audio API: " + (app.scan.audioApi.empty() ? std::string("(none)") : app.scan.audioApi) + ", " +
                 std::to_string(app.scan.audioDevices.size()) + " audio devices");
    for (const auto& d : app.scan.audioDevices) {
        app.log.info("  audio device \"" + d.name + "\": in " + std::to_string(d.inputChannels) + ", out " +
                     std::to_string(d.outputChannels));
    }
    for (const auto& p : app.scan.midiInputs) app.log.info("  MIDI input \"" + p.name + "\"");
    for (const auto& w : app.scan.warnings) app.log.warning(w);
}

void logOpenResult(AppState& app, const minidaw::OpenResult& r)
{
    for (const auto& w : r.warnings) app.log.warning("Audio: " + w);
    if (!r.error.empty()) {
        app.log.error("Audio: " + r.error);
        return;
    }
    const auto& info = app.audio.info();
    char buf[512];
    std::snprintf(buf, sizeof buf, "Audio stream running: %s, %u Hz, %u frames; input \"%s\" (%u ch), output \"%s\" (%u ch)",
                  info.api.c_str(), info.sampleRate, info.bufferFrames,
                  info.inputName.empty() ? "(none)" : info.inputName.c_str(), info.inputChannels,
                  info.outputName.empty() ? "(none)" : info.outputName.c_str(), info.outputChannels);
    app.log.info(buf);
}

minidaw::EnforceResult buildPalette(const ContrastSetting& setting)
{
    return minidaw::enforceMinimumContrast(minidaw::defaultPalette(), setting.minimum());
}

// Apply a new contrast setting to the GUI and store it in the config file.
void setContrast(AppState& app, const ContrastSetting& setting, bool save)
{
    app.config.contrast = setting;
    auto result = buildPalette(setting);
    app.palette = result.palette;
    app.unresolved = result.unresolved;
    minidaw::app::applyPalette(app.palette);
    if (save) saveConfig(app);
}

// Ctrl+Shift+Up / Down: step through AA -> AAA -> Max.
void stepContrast(AppState& app, int direction)
{
    const ContrastSetting steps[] = {{ContrastPreset::AA}, {ContrastPreset::AAA}, {ContrastPreset::Max}};
    const double current = app.config.contrast.minimum();
    if (direction > 0) {
        for (const auto& s : steps) {
            if (s.minimum() > current + 1e-9) { setContrast(app, s, true); return; }
        }
    } else {
        for (int i = 2; i >= 0; --i) {
            if (steps[i].minimum() < current - 1e-9) { setContrast(app, steps[i], true); return; }
        }
    }
}

// Text size (SPEC-UI-08). Applied through ImGui's dynamic font size.
void setFontSize(AppState& app, float size, bool save)
{
    app.config.fontSize = std::clamp(size, minidaw::kMinFontSize, minidaw::kMaxFontSize);
    ImGui::GetStyle().FontSizeBase = app.config.fontSize;
    if (save) saveConfig(app);
}

std::string describeContrast(const ContrastSetting& s)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s (minimum %.4g:1)", s.toString().c_str(), s.minimum());
    return buf;
}

// Open the audio stream with the given settings. On success the settings are
// kept (and saved if `save`); on failure the previous settings are reopened.
void refreshTakeLength(AppState& app, minidaw::Track& t)
{
    const unsigned int rate = app.audio.info().sampleRate ? app.audio.info().sampleRate : app.config.audio.sampleRate;
    t.takeFrames = minidaw::takeLengthFrames(t, rate);
}

void stopPlayback(AppState& app)
{
    app.midiPlayer.stop();      // first: its thread reads the player's run
    app.audio.player().stop();  // keeps the position where it stopped
    app.playbackForRecording = false;
}

void applyAudioSettings(AppState& app, const minidaw::AudioSettings& settings, bool save)
{
    if (app.session.recording()) {
        app.audioError = "Stop the recording before changing the audio devices.";
        return;
    }
    stopPlayback(app);  // the MIDI thread reads the stream; the run does not survive a reopen
    const minidaw::AudioSettings previous = app.config.audio;
    minidaw::OpenResult r = app.audio.open(settings, app.scan);
    logOpenResult(app, r);
    app.audioWarnings = r.warnings;
    app.audioError = r.error;
    if (r.error.empty()) {
        app.config.audio = settings;
        if (save) saveConfig(app);
        for (auto& t : app.tracks) refreshTakeLength(app, t);  // MIDI lengths depend on the rate (M9)
    } else if (!(previous == settings)) {
        minidaw::OpenResult back = app.audio.open(previous, app.scan);
        logOpenResult(app, back);
        if (back.error.empty()) {
            app.audioWarnings.push_back("The previous audio settings are active again.");
        }
    }
    for (auto& m : app.inputMeters) m.reset();
    for (auto& m : app.outputMeters) m.reset();
}

// ---------------------------------------------------------------------------
// Text output
// ---------------------------------------------------------------------------
void printDevices(const minidaw::DeviceScan& scan)
{
    std::printf("Audio API: %s\n", scan.audioApi.empty() ? "(none)" : scan.audioApi.c_str());
    std::printf("Audio devices (%zu):\n", scan.audioDevices.size());
    for (const auto& d : scan.audioDevices) {
        std::printf("  [%u] %s  in:%u out:%u  %s%s preferred:%u Hz\n", d.id, d.name.c_str(),
                    d.inputChannels, d.outputChannels, d.isDefaultInput ? "default-in " : "",
                    d.isDefaultOutput ? "default-out " : "", d.preferredSampleRate);
    }
    std::printf("MIDI inputs (%zu):\n", scan.midiInputs.size());
    for (const auto& p : scan.midiInputs) std::printf("  [%u] %s\n", p.index, p.name.c_str());
    std::printf("MIDI outputs (%zu):\n", scan.midiOutputs.size());
    for (const auto& p : scan.midiOutputs) std::printf("  [%u] %s\n", p.index, p.name.c_str());
    for (const auto& w : scan.warnings) std::printf("warning: %s\n", w.c_str());
}

int printContrastReport(const AppState& app)
{
    std::printf("Contrast setting: %s\n", describeContrast(app.config.contrast).c_str());
    std::printf("%-22s %-22s %-8s %-8s %8s  %s\n", "Foreground", "Background", "FG", "BG", "Ratio", "Result");
    for (const auto& pair : minidaw::colorPairs()) {
        const auto& fg = app.palette[pair.foreground];
        const auto& bg = app.palette[pair.background];
        const double r = minidaw::contrastRatio(fg, bg);
        std::printf("%-22s %-22s %-8s %-8s %7.2f:1  %s\n", std::string(minidaw::roleName(pair.foreground)).c_str(),
                    std::string(minidaw::roleName(pair.background)).c_str(), minidaw::toHex(fg).c_str(),
                    minidaw::toHex(bg).c_str(), r,
                    minidaw::meetsContrast(r, app.config.contrast.minimum()) ? "pass" : "FAIL");
    }
    return app.unresolved.empty() ? 0 : 1;
}

// --audio-test: open the configured devices, run, report (T-DEV-02 without a GUI).
int runAudioTest(AppState& app, int seconds, bool monitor)
{
    app.scan = minidaw::scanDevices();
    logScan(app);
    const auto& s = app.config.audio;
    std::printf("Requested: input %s, output %s, %u Hz, %u frames\n", s.inputDevice.c_str(),
                s.outputDevice.c_str(), s.sampleRate, s.bufferFrames);
    minidaw::OpenResult r = app.audio.open(s, app.scan);
    logOpenResult(app, r);
    for (const auto& w : r.warnings) std::printf("warning: %s\n", w.c_str());
    if (!r.error.empty()) {
        std::printf("error: %s\n", r.error.c_str());
        return 1;
    }
    const auto& info = app.audio.info();
    std::printf("Running:   %s, %u Hz, %u frames, reported latency %.1f ms\n", info.api.c_str(), info.sampleRate,
                info.bufferFrames, info.reportedLatencyMs);
    std::printf("  input:  %s (%u ch)\n", info.inputName.empty() ? "(none)" : info.inputName.c_str(),
                info.inputChannels);
    std::printf("  output: %s (%u ch)\n", info.outputName.empty() ? "(none)" : info.outputName.c_str(),
                info.outputChannels);

    std::array<float, minidaw::kMaxInputChannels> peak{};
    std::array<float, minidaw::kMaxOutputChannels> outPeak{};
    if (monitor) {
        // The default track (inputs 1+2, or 1), armed with monitoring on (T-MON-01 without a GUI).
        std::vector<minidaw::Track> tracks = minidaw::defaultTracks(info.inputChannels);
        for (auto& t : tracks) t.armed = t.monitor = true;
        app.audio.setMonitor(minidaw::monitorGains(tracks, info.inputChannels));
        if (tracks.empty() || info.outputChannels == 0) {
            std::printf("warning: monitoring needs an input and an output\n");
        } else {
            std::printf("Monitoring: %s to the output\n",
                        tracks[0].channels.size() == 2 ? "inputs 1+2 (stereo)" : "input 1 (mono)");
        }
    }
    // Measure from a fixed starting point: the count already includes the callbacks
    // made while open() waited, and Windows' sleep_for() overshoots, so use real time.
    const std::uint64_t startCallbacks = app.audio.callbacks();
    const auto startTime = std::chrono::steady_clock::now();
    for (int t = 0; t < seconds * 10; ++t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
            peak[ch] = std::max(peak[ch], app.audio.takeInputPeak(ch));
        }
        for (unsigned int ch = 0; ch < info.outputChannels && ch < minidaw::kMaxOutputChannels; ++ch) {
            outPeak[ch] = std::max(outPeak[ch], app.audio.takeOutputPeak(ch));
        }
    }
    const std::uint64_t callbacks = app.audio.callbacks() - startCallbacks;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    const double expected = info.bufferFrames ? elapsed * info.sampleRate / info.bufferFrames : 0.0;
    for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
        std::printf("%s in %2u: %-10s", ch % 4 == 0 ? "  peak" : "", ch + 1, minidaw::formatDbfs(peak[ch]).c_str());
        if (ch % 4 == 3 || ch + 1 == info.inputChannels) std::printf("\n");
    }
    if (monitor) {
        for (unsigned int ch = 0; ch < info.outputChannels && ch < minidaw::kMaxOutputChannels; ++ch) {
            std::printf("%s out %u: %-10s", ch == 0 ? "  peak" : "", ch + 1, minidaw::formatDbfs(outPeak[ch]).c_str());
        }
        std::printf("\n");
    }
    std::printf("Callbacks: %llu in %.2f s (expected about %.0f), dropouts (xruns): %llu\n",
                static_cast<unsigned long long>(callbacks), elapsed, expected,
                static_cast<unsigned long long>(app.audio.xruns()));
    for (const auto& m : app.audio.takeDriverMessages()) std::printf("driver: %s\n", m.c_str());
    app.audio.close();
    return app.audio.callbacks() > 0 ? 0 : 1;
}

std::vector<std::string> describeResult(const minidaw::SessionResult& res)
{
    std::vector<std::string> lines;
    char buf[1024];
    std::snprintf(buf, sizeof buf, "Take folder %s, %.2f s", minidaw::pathToUtf8(res.folder).c_str(), res.seconds);
    lines.emplace_back(buf);
    for (const auto& t : res.audio.takes) {
        std::snprintf(buf, sizeof buf, "%s: %llu frames, lost frames %llu, clipped samples %llu",
                      minidaw::pathToUtf8(t.path.filename()).c_str(), static_cast<unsigned long long>(t.frames),
                      static_cast<unsigned long long>(t.droppedFrames), static_cast<unsigned long long>(t.clippedSamples));
        lines.emplace_back(buf);
    }
    for (const auto& m : res.midi) {
        std::snprintf(buf, sizeof buf, "%s: %zu MIDI events from \"%s\", lost messages %llu",
                      minidaw::pathToUtf8(m.path.filename()).c_str(), m.events, m.port.c_str(),
                      static_cast<unsigned long long>(m.lostMessages));
        lines.emplace_back(buf);
        if (m.disconnects > 0) {
            std::snprintf(buf, sizeof buf,
                          "%s: the MIDI device was disconnected %llu time(s) during the take; MIDI played while it "
                          "was gone is missing",
                          minidaw::pathToUtf8(m.path.filename()).c_str(), static_cast<unsigned long long>(m.disconnects));
            lines.emplace_back(buf);
        }
    }
    if (!res.audio.error.empty()) lines.push_back("error: " + res.audio.error);
    if (!res.error.empty()) lines.push_back("error: " + res.error);
    return lines;
}

// --record-test: record input 1 (and 2) and every MIDI input into a take
// folder in `folder` (T-REC-10, SPEC-REC-06), through the same recording
// session as the GUI.
// Log a MIDI port event and return its text (SPEC-REC-01b, SPEC-LOG-01).
std::string noteMidiEvent(AppState& app, const minidaw::MidiPortEvent& e, bool recording)
{
    using Kind = minidaw::MidiPortEvent::Kind;
    const std::string port = "MIDI input \"" + e.port + "\"";
    const std::string as = e.detail != e.port && !e.detail.empty() ? " (as \"" + e.detail + "\")" : "";
    std::string text;
    switch (e.kind) {
    case Kind::Opened:
        text = port + " opened" + as + ".";
        app.log.info(text);
        break;
    case Kind::Reconnected:
        text = port + " is connected again" + as + (recording ? "; recording from it goes on." : ".");
        app.log.info(text);
        break;
    case Kind::Disconnected:
        text = port + " was disconnected" +
               (recording ? " during the recording: MIDI played while it is gone is missing." : ".") +
               " It is opened again when it comes back.";
        app.log.warning(text);
        break;
    case Kind::Failed:
        text = e.detail + " Trying again every 2 seconds.";
        app.log.warning(text);
        break;
    case Kind::Ambiguous:
        text = port + as + ": one of several identical MIDI devices was unplugged and mini-daw cannot tell which, " +
               "so all of them are closed" + (recording ? " (MIDI from them is missing from now on)" : "") +
               ". Arm the track off and on to open it again.";
        app.log.warning(text);
        break;
    }
    return text;
}

int runRecordTest(AppState& app, int seconds, const std::filesystem::path& folder)
{
    app.scan = minidaw::scanDevices();
    for (const auto& w : app.scan.warnings) std::printf("warning: %s\n", w.c_str());
    logScan(app);
    minidaw::OpenResult r = app.audio.open(app.config.audio, app.scan);
    logOpenResult(app, r);
    for (const auto& w : r.warnings) std::printf("warning: %s\n", w.c_str());
    if (!r.error.empty()) {
        std::printf("error: %s\n", r.error.c_str());
        return 1;
    }
    const auto& info = app.audio.info();

    std::vector<minidaw::Track> tracks = minidaw::defaultTracks(info.inputChannels);
    std::vector<std::string> ports;
    for (const auto& p : app.scan.midiInputs) {
        minidaw::Track t;
        t.number = minidaw::nextTrackNumber(tracks);
        t.kind = minidaw::TrackKind::Midi;
        t.name = "MIDI " + std::to_string(t.number);
        t.midiPort = p.name;
        tracks.push_back(t);
        ports.push_back(p.name);
    }
    for (const auto& e : app.midi.setPorts(ports)) {
        const std::string text = noteMidiEvent(app, e, false);
        if (e.kind == minidaw::MidiPortEvent::Kind::Failed) std::printf("warning: %s\n", text.c_str());
    }
    for (auto& t : tracks) t.armed = t.kind == minidaw::TrackKind::Audio || app.midi.isOpen(t.midiPort);
    if (std::none_of(tracks.begin(), tracks.end(), [](const minidaw::Track& t) { return t.armed; })) {
        std::printf("error: no audio input and no MIDI input is open, so there is nothing to record.\n");
        return 1;
    }

    minidaw::RecordingSettings settings = app.config.recording;
    settings.folder = minidaw::pathToUtf8(folder);
    std::string error;
    if (!app.session.start(tracks, settings, app.audio, app.midi, error)) {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }
    std::printf("Recording into %s for %d s at %u Hz:\n", minidaw::pathToUtf8(app.session.folder()).c_str(), seconds,
                info.sampleRate);
    for (const auto& t : tracks) {
        if (!t.armed) continue;
        if (t.kind == minidaw::TrackKind::Audio) {
            std::printf("  track %d: %s, %zu ch\n", t.number, info.inputName.c_str(), t.channels.size());
        } else {
            std::printf("  track %d: MIDI \"%s\", all channels\n", t.number, t.midiPort.c_str());
        }
    }
    app.log.info("Recording test into " + minidaw::pathToUtf8(app.session.folder()));
    for (int t = 0; t < seconds * 10; ++t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!app.session.diskError().empty()) {
            app.log.error("Recording: " + app.session.diskError());
            break;
        }
        if (t % 20 == 19) {  // hot-plug check every 2 s, as in the GUI
            for (const auto& e : app.midi.checkConnections()) {
                std::printf("%s\n", noteMidiEvent(app, e, true).c_str());
            }
        }
    }
    const minidaw::SessionResult res = app.session.stop();
    const std::uint64_t xruns = app.audio.xruns();
    app.midi.closeAll();
    app.audio.close();

    for (const auto& line : describeResult(res)) {
        std::printf("%s\n", line.c_str());
        app.log.info("Take: " + line);
    }
    std::printf("dropouts (xruns): %llu\n", static_cast<unsigned long long>(xruns));
    if (res.lostData()) app.log.warning("The recording test lost data.");
    return res.lostData() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// GUI panels
// ---------------------------------------------------------------------------
void openAudioSettings(AppState& app)
{
    app.pendingAudio = app.config.audio;
    app.showAudioSettings = true;
}

// Projects (defined further down, with the playback code they use).
void showProjectDialog(AppState& app, AppState::ProjectDialog kind);
bool openProject(AppState& app, const std::filesystem::path& folder);
void closeProject(AppState& app);
std::filesystem::path activeTakesFolder(const AppState& app);

void drawMenuBar(AppState& app, bool& quit)
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        const bool rec = app.session.recording();
        if (ImGui::MenuItem("New project...", nullptr, false, !rec)) showProjectDialog(app, AppState::ProjectDialog::New);
        if (ImGui::MenuItem("Open project...", nullptr, false, !rec)) showProjectDialog(app, AppState::ProjectDialog::Open);
        if (ImGui::BeginMenu("Recent projects", !rec && !app.config.project.recent.empty())) {
            const auto recent = app.config.project.recent;  // a copy: opening changes the list
            for (const auto& r : recent) {
                if (ImGui::MenuItem(r.c_str())) openProject(app, minidaw::pathFromUtf8(r));
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save project as...", nullptr, false, !rec)) {
            showProjectDialog(app, AppState::ProjectDialog::SaveAs);
        }
        if (ImGui::MenuItem("Close project", nullptr, false, !rec && !app.projectFolder.empty())) closeProject(app);
        ImGui::Separator();
        if (ImGui::MenuItem("Open take folder...", nullptr, false, !app.session.recording())) {
            app.showTakeBrowser = true;
            app.takeFolders = minidaw::listTakeFolders(activeTakesFolder(app));
            app.takeBrowserError.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Audio devices...")) openAudioSettings(app);
        if (ImGui::MenuItem("Recording...")) {
            app.pendingRecording = app.config.recording;
            app.showRecordingSettings = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Contrast")) {
            const auto p = app.config.contrast.preset;
            if (ImGui::MenuItem("AA  (4.5:1)", nullptr, p == ContrastPreset::AA))
                setContrast(app, {ContrastPreset::AA}, true);
            if (ImGui::MenuItem("AAA (7:1)", nullptr, p == ContrastPreset::AAA))
                setContrast(app, {ContrastPreset::AAA}, true);
            if (ImGui::MenuItem("Max (21:1, black on white)", nullptr, p == ContrastPreset::Max))
                setContrast(app, {ContrastPreset::Max}, true);
            if (ImGui::MenuItem("Custom...", nullptr, p == ContrastPreset::Custom)) {
                app.customInput = app.config.contrast.minimum();
                app.customError.clear();
                app.showCustomContrast = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Raise contrast", "Ctrl+Shift+Up")) stepContrast(app, +1);
            if (ImGui::MenuItem("Lower contrast", "Ctrl+Shift+Down")) stepContrast(app, -1);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Text size")) {
            ImGui::Text("Current: %d px", static_cast<int>(app.config.fontSize));
            if (ImGui::MenuItem("Larger", "Ctrl+=")) setFontSize(app, app.config.fontSize + 2, true);
            if (ImGui::MenuItem("Smaller", "Ctrl+-")) setFontSize(app, app.config.fontSize - 2, true);
            if (ImGui::MenuItem("Default (16 px)", "Ctrl+0")) setFontSize(app, minidaw::kDefaultFontSize, true);
            ImGui::EndMenu();
        }
        ImGui::MenuItem("Contrast report", nullptr, &app.showContrastReport);
        ImGui::Separator();
        ImGui::MenuItem("Log", "Ctrl+L", &app.showLog);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void drawCustomContrastWindow(AppState& app)
{
    if (!app.showCustomContrast) return;
    ImGui::SetNextWindowSize(ImVec2(30 * ImGui::GetFontSize(), 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Custom contrast", &app.showCustomContrast, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextWrapped("Minimum contrast ratio for every colour pair, from 4.5 (WCAG AA) to 21 "
                           "(pure black on white).");
        ImGui::SetNextItemWidth(12 * ImGui::GetFontSize());
        ImGui::InputDouble("ratio : 1", &app.customInput, 0.5, 1.0, "%.2f");
        if (ImGui::Button("Apply")) {
            app.customError = minidaw::validateCustomContrast(app.customInput);
            if (app.customError.empty()) {
                setContrast(app, {ContrastPreset::Custom, app.customInput}, true);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) app.showCustomContrast = false;
        if (!app.customError.empty()) {
            // Shown as text with a label, not by colour alone.
            ImGui::TextWrapped("Not applied: %s", app.customError.c_str());
        }
    }
    ImGui::End();
}

void drawContrastReportWindow(AppState& app)
{
    if (!app.showContrastReport) return;
    ImGui::SetNextWindowSize(ImVec2(60 * ImGui::GetFontSize(), 30 * ImGui::GetFontSize()), ImGuiCond_Appearing);
    if (ImGui::Begin("Contrast report", &app.showContrastReport)) {
        const auto lowest = minidaw::lowestContrast(app.palette);
        ImGui::Text("Setting: %s", describeContrast(app.config.contrast).c_str());
        ImGui::Text("Lowest pair: %.2f:1 (%s on %s)", lowest.ratio,
                    std::string(minidaw::roleName(lowest.pair.foreground)).c_str(),
                    std::string(minidaw::roleName(lowest.pair.background)).c_str());
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("pairs", 6, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Foreground");
            ImGui::TableSetupColumn("Background");
            ImGui::TableSetupColumn("FG");
            ImGui::TableSetupColumn("BG");
            ImGui::TableSetupColumn("Ratio");
            ImGui::TableSetupColumn("Result");
            ImGui::TableHeadersRow();
            for (const auto& pair : minidaw::colorPairs()) {
                const auto& fg = app.palette[pair.foreground];
                const auto& bg = app.palette[pair.background];
                const double r = minidaw::contrastRatio(fg, bg);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(std::string(minidaw::roleName(pair.foreground)).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(std::string(minidaw::roleName(pair.background)).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(minidaw::toHex(fg).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(minidaw::toHex(bg).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%.2f:1", r);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(minidaw::meetsContrast(r, app.config.contrast.minimum()) ? "pass" : "FAIL");
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Combo box listing (default), (none) and every device usable in `dir`.
void deviceCombo(const char* label, std::string& value, const minidaw::DeviceScan& scan, minidaw::Direction dir)
{
    const bool input = dir == minidaw::Direction::Input;
    std::string preview = value;
    if (value != minidaw::kDefaultDevice && value != minidaw::kNoDevice) {
        const bool present = std::any_of(scan.audioDevices.begin(), scan.audioDevices.end(), [&](const auto& d) {
            return d.name == value && (input ? d.inputChannels : d.outputChannels) > 0;
        });
        if (!present) preview += "  (not found)";
    }
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable(minidaw::kDefaultDevice, value == minidaw::kDefaultDevice)) value = minidaw::kDefaultDevice;
        if (ImGui::Selectable(minidaw::kNoDevice, value == minidaw::kNoDevice)) value = minidaw::kNoDevice;
        for (const auto& d : scan.audioDevices) {
            const unsigned int ch = input ? d.inputChannels : d.outputChannels;
            if (ch == 0) continue;
            ImGui::PushID(static_cast<int>(d.id));
            const std::string text = d.name + "  (" + std::to_string(ch) + " ch)";
            if (ImGui::Selectable(text.c_str(), value == d.name)) value = d.name;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

template <std::size_t N>
void numberCombo(const char* label, unsigned int& value, const unsigned int (&choices)[N], const char* unit)
{
    const std::string current = std::to_string(value) + " " + unit;
    if (ImGui::BeginCombo(label, current.c_str())) {
        for (unsigned int c : choices) {
            const std::string text = std::to_string(c) + " " + unit;
            if (ImGui::Selectable(text.c_str(), value == c)) value = c;
        }
        ImGui::EndCombo();
    }
}

void drawAudioSettingsWindow(AppState& app)
{
    if (!app.showAudioSettings) return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(40 * em, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Audio devices", &app.showAudioSettings, ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Audio API: %s", app.scan.audioApi.c_str());
        ImGui::SetNextItemWidth(26 * em);
        deviceCombo("Input device", app.pendingAudio.inputDevice, app.scan, minidaw::Direction::Input);
        ImGui::SetNextItemWidth(26 * em);
        deviceCombo("Output device", app.pendingAudio.outputDevice, app.scan, minidaw::Direction::Output);
        ImGui::SetNextItemWidth(12 * em);
        numberCombo("Sample rate", app.pendingAudio.sampleRate, minidaw::kSampleRateChoices, "Hz");
        ImGui::SetNextItemWidth(12 * em);
        numberCombo("Buffer size", app.pendingAudio.bufferFrames, minidaw::kBufferSizeChoices, "frames");
        if (app.pendingAudio.sampleRate > 0) {
            ImGui::Text("One buffer = %.1f ms", 1000.0 * app.pendingAudio.bufferFrames / app.pendingAudio.sampleRate);
        }
        if (ImGui::Button("Apply")) applyAudioSettings(app, app.pendingAudio, true);
        ImGui::SameLine();
        if (ImGui::Button("Rescan devices")) {
            app.scan = minidaw::scanDevices();
            app.log.info("Devices rescanned");
            logScan(app);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) app.showAudioSettings = false;
        if (!app.audioError.empty()) ImGui::TextWrapped("Error: %s", app.audioError.c_str());
        for (const auto& w : app.audioWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    }
    ImGui::End();
}

ImU32 roleColor(const AppState& app, minidaw::Role role)
{
    const auto& c = app.palette[role];
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), 1.0f));
}

// Level meter (SPEC-MON-02): one bar per channel, -60 .. 0 dBFS, with marks at
// -18 and -6 dBFS and a peak-hold line. Beside it the level as dBFS text and,
// after clipping, a CLIP button that clears it: the level never depends on
// colour alone (SPEC-UI-07). Colours: Accent and Text on Control, as checked
// by colorPairs().
void drawMeter(AppState& app, const std::vector<minidaw::LevelMeter*>& meters, float width)
{
    if (meters.empty()) return;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float n = static_cast<float>(meters.size());
    const float gap = meters.size() > 1 ? 2.0f : 0.0f;
    const float barH = (h - 2.0f - gap * (n - 1.0f)) / n;
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), roleColor(app, minidaw::Role::Control));
    float text = 0.0f;
    bool clipped = false;
    for (std::size_t i = 0; i < meters.size(); ++i) {
        const minidaw::LevelMeter& m = *meters[i];
        const float y0 = p.y + 1.0f + static_cast<float>(i) * (barH + gap);
        const float fill = minidaw::meterFraction(m.bar()) * (width - 2.0f);
        if (fill > 0.0f) {
            dl->AddRectFilled(ImVec2(p.x + 1.0f, y0), ImVec2(p.x + 1.0f + fill, y0 + barH),
                              roleColor(app, minidaw::Role::Accent));
        }
        if (m.hold() > 0.0f) {
            const float x = p.x + 1.0f + minidaw::meterFraction(m.hold()) * (width - 2.0f);
            dl->AddRectFilled(ImVec2(x - 1.5f, y0), ImVec2(x + 0.5f, y0 + barH), roleColor(app, minidaw::Role::Text));
        }
        text = std::max(text, m.text());
        clipped = clipped || m.clipped();
    }
    for (const float db : {-18.0f, -6.0f}) {
        const float x = p.x + 1.0f + minidaw::meterFraction(std::pow(10.0f, db / 20.0f)) * (width - 2.0f);
        dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + h * 0.3f), roleColor(app, minidaw::Role::Text));
        dl->AddLine(ImVec2(x, p.y + h * 0.7f), ImVec2(x, p.y + h), roleColor(app, minidaw::Role::Text));
    }
    dl->AddRect(p, ImVec2(p.x + width, p.y + h), roleColor(app, minidaw::Role::Border));
    ImGui::Dummy(ImVec2(width, h));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Level meter, -60 to 0 dBFS. Marks at -18 and -6 dBFS; the thin line is the peak of the "
                          "last 1.5 s.");
    }
    ImGui::SameLine();
    const std::string value = minidaw::formatDbfs(text);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value.c_str());
    const float pad = 5.5f * ImGui::GetFontSize() - ImGui::CalcTextSize(value.c_str()).x;
    if (pad > 0.0f) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(pad, 1.0f));
    }
    if (clipped) {
        ImGui::SameLine();
        if (ImGui::SmallButton("CLIP")) {
            for (auto* m : meters) m->clearClip();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The level reached 0 dBFS. Click to clear.");
    }
}

// Once per frame: feed the meters and hand changed monitoring gains to the
// audio stream (SPEC-MON-01/02).
void updateMeters(AppState& app)
{
    const auto& info = app.audio.info();
    const double now = ImGui::GetTime();
    // Watchdog: a running stream whose callbacks stop (driver hang, device
    // gone) would otherwise look normal but silent.
    const std::uint64_t callbacks = app.audio.callbacks();
    if (!info.running || callbacks != app.lastCallbacks) {
        if (app.streamStalled) app.log.info("Audio callbacks arrive again.");
        app.lastCallbacks = callbacks;
        app.callbacksChangedAt = now;
        app.streamStalled = false;
    } else if (!app.streamStalled && now - app.callbacksChangedAt > 1.0) {
        app.streamStalled = true;
        app.log.error("The audio stream has stopped: no audio callback for more than 1 second. Playback, "
                      "recording and the meters are stuck; try Settings > Audio devices > Apply.");
    }
    if (info.running) {
        for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
            app.inputMeters[ch].update(app.audio.takeInputPeak(ch), now);
        }
        for (unsigned int ch = 0; ch < info.outputChannels && ch < minidaw::kMaxOutputChannels; ++ch) {
            app.outputMeters[ch].update(app.audio.takeOutputPeak(ch), now);
        }
    }
    const minidaw::MonitorGains gains = info.running && info.outputChannels > 0
                                            ? minidaw::monitorGains(app.tracks, info.inputChannels)
                                            : minidaw::MonitorGains{};
    if (!(gains == app.monitorSent)) {
        app.audio.setMonitor(gains);
        app.monitorSent = gains;
    }
    for (const auto& t : app.tracks) {
        if (t.kind != minidaw::TrackKind::Midi || !app.midi.isOpen(t.midiPort)) continue;
        auto& a = app.midiActivity[t.midiPort];
        const std::uint64_t count = app.midi.messagesSeen(t.midiPort);
        if (count != a.count) {
            a.count = count;
            a.changedAt = now;
        }
    }
}

void drawAudioStatus(AppState& app)
{
    ImGui::SeparatorText("Audio stream");
    if (ImGui::Button("Audio settings...")) openAudioSettings(app);
    const auto& info = app.audio.info();
    if (!info.running) {
        ImGui::TextWrapped("Stopped. %s", app.audioError.c_str());
    } else {
        if (app.streamStalled) {
            ImGui::TextWrapped("ERROR: the audio stream has stopped (no audio callback for more than 1 second). "
                               "Try Settings > Audio devices > Apply.");
        }
        ImGui::Text("Running: %s, %u Hz, %u frames (%.1f ms per buffer), reported latency %.1f ms", info.api.c_str(),
                    info.sampleRate, info.bufferFrames, 1000.0 * info.bufferFrames / info.sampleRate,
                    info.reportedLatencyMs);
        ImGui::Text("Input:  %s (%u ch)", info.inputName.empty() ? "(none)" : info.inputName.c_str(),
                    info.inputChannels);
        ImGui::Text("Output: %s (%u ch)", info.outputName.empty() ? "(none)" : info.outputName.c_str(),
                    info.outputChannels);
        ImGui::Text("Dropouts (xruns): %llu", static_cast<unsigned long long>(app.audio.xruns()));

        if (info.outputChannels > 0) {
            ImGui::TextUnformatted("Output level:");
            ImGui::SameLine();
            std::vector<minidaw::LevelMeter*> out;
            for (unsigned int ch = 0; ch < info.outputChannels && ch < minidaw::kMaxOutputChannels; ++ch) {
                out.push_back(&app.outputMeters[ch]);
            }
            drawMeter(app, out, 10 * ImGui::GetFontSize());
        }
        // Each track shows its own level; the full list is folded away (crosscheck #2, G3).
        // As numbers, not colour alone (SPEC-UI-07).
        const std::string header = "Input levels (" + std::to_string(info.inputChannels) + " channels)###levels";
        if (info.inputChannels > 0 && ImGui::TreeNode(header.c_str())) {
            for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
                if (ch % 4 != 0) ImGui::SameLine(static_cast<float>(ch % 4) * 14 * ImGui::GetFontSize());
                const auto& m = app.inputMeters[ch];
                ImGui::Text("In %u: %s%s", ch + 1, minidaw::formatDbfs(m.text()).c_str(), m.clipped() ? " CLIP" : "");
            }
            ImGui::TreePop();
        }
    }
    for (const auto& w : app.audioWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    for (const auto& m : app.audio.takeDriverMessages()) {
        app.log.warning("Audio driver: " + m);
        app.audioWarnings.push_back(m);
    }
}

// --- Tracks and recording (P6) ---------------------------------------------
// The default track is added once, when the stream first runs, and only if
// the user has not made any track yet (crosscheck #4, J3).
void ensureDefaultTracks(AppState& app)
{
    if (app.tracksCreated) return;
    if (!app.tracks.empty()) {
        app.tracksCreated = true;
        return;
    }
    if (!app.audio.info().running) return;
    app.tracks = minidaw::defaultTracks(app.audio.info().inputChannels);
    app.tracksCreated = true;
}

// Open exactly the MIDI ports of armed MIDI tracks (Windows lets only one
// program open a port, so unarmed tracks do not hold it).
// A port that does not open (unplugged, used by another program) is tried
// again every 2 seconds, and right before Record (crosscheck #4, J2).
void showMidiEvents(AppState& app, const std::vector<minidaw::MidiPortEvent>& events)
{
    for (const auto& e : events) {
        app.midiWarnings.push_back(noteMidiEvent(app, e, app.session.recording()));
        if (app.midiWarnings.size() > 4) app.midiWarnings.erase(app.midiWarnings.begin());
    }
}

// Open the ports of armed MIDI tracks; every 2 s re-read the port list:
// new devices appear in the lists, unplugged ones are noticed and opened
// again when they return, also while recording (SPEC-REC-01a/01b).
void syncMidiPorts(AppState& app, bool force = false)
{
    const double now = ImGui::GetTime();
    if (!app.session.recording() && (force || app.midiPortsDirty)) {
        std::vector<std::string> ports;
        for (const auto& t : app.tracks) {
            if (t.kind == minidaw::TrackKind::Midi && t.armed && !t.midiPort.empty()) ports.push_back(t.midiPort);
        }
        showMidiEvents(app, app.midi.setPorts(ports));
        app.midiPortsDirty = false;
    }
    if (!force && now < app.midiCheckAt) return;
    app.midiCheckAt = now + 2.0;
    showMidiEvents(app, app.midi.checkConnections());
    const std::vector<std::string> names = app.midi.availablePorts();
    std::vector<minidaw::MidiPortInfo> list;
    for (unsigned int i = 0; i < names.size(); ++i) list.push_back({i, names[i]});
    const bool same = list.size() == app.scan.midiInputs.size() &&
                      std::equal(list.begin(), list.end(), app.scan.midiInputs.begin(),
                                 [](const auto& a, const auto& b) { return a.name == b.name; });
    if (!same) {
        std::string text = "MIDI inputs now:";
        for (const auto& n : names) text += " \"" + n + "\"";
        if (names.empty()) text += " (none)";
        app.log.info(text);
        app.scan.midiInputs = std::move(list);
    }
}

// --- Projects (P9) ---------------------------------------------------------
// Where projects are proposed: <recording folder>/projects.
std::filesystem::path projectsBase(const AppState& app)
{
    return minidaw::recordingFolder(app.config.recording) / "projects";
}

// Takes go into the open project, else into the recording folder (SPEC-PRJ-01).
std::filesystem::path activeTakesFolder(const AppState& app)
{
    return app.projectFolder.empty() ? minidaw::recordingFolder(app.config.recording) : app.projectFolder / "takes";
}

minidaw::ProjectData currentProject(const AppState& app)
{
    minidaw::ProjectData d;
    d.tracks = app.tracks;
    d.sampleRate = app.audio.info().sampleRate ? app.audio.info().sampleRate : app.config.audio.sampleRate;
    return d;
}

// Writes the project if anything changed (autosave, SPEC-PRJ-02a).
void saveProjectNow(AppState& app)
{
    if (app.projectFolder.empty()) return;
    const minidaw::ProjectData d = currentProject(app);
    const std::string text = minidaw::projectToJson(d, app.projectFolder);
    if (text == app.projectSavedText) return;
    std::string err;
    if (minidaw::saveProject(app.projectFolder, d, err)) {
        app.projectSavedText = text;
        if (!app.projectError.empty()) app.log.info("Project saved again.");
        app.projectError.clear();
    } else if (err != app.projectError) {
        app.projectError = err;
        app.log.error("Project not saved: " + err);
    }
}

void autosaveProject(AppState& app)
{
    const double now = ImGui::GetTime();
    if (now < app.projectCheckAt) return;
    app.projectCheckAt = now + 0.5;
    saveProjectNow(app);
}

// Copy takes from outside into the project (the user's choice: a project
// folder is complete on its own).
void importIntoProject(AppState& app)
{
    if (app.projectFolder.empty()) return;
    std::vector<std::string> copied;
    for (const auto& e : minidaw::importTakes(app.projectFolder, app.tracks, copied)) {
        app.log.warning(e);
        app.projectNotes.push_back(e);
    }
    for (const auto& c : copied) app.log.info("Copied into the project: " + c);
}

void rememberProjectInConfig(AppState& app)
{
    const std::string folder = minidaw::pathToUtf8(app.projectFolder);
    app.config.project.last = folder;
    if (!folder.empty()) minidaw::rememberProject(app.config.project, folder);
    saveConfig(app);
}

void stopPlayback(AppState& app);
void refreshTakeLength(AppState& app, minidaw::Track& t);

bool openProject(AppState& app, const std::filesystem::path& folder)
{
    if (app.session.recording()) {
        app.projectDialogError = "Stop the recording first.";
        return false;
    }
    minidaw::ProjectData d;
    std::vector<std::string> warnings;
    std::string err;
    if (!minidaw::loadProject(folder, d, warnings, err)) {
        app.projectDialogError = err;
        app.log.error("Open project: " + err);
        return false;
    }
    saveProjectNow(app);  // the current one first
    stopPlayback(app);
    app.projectFolder = folder;
    app.tracks = std::move(d.tracks);
    app.tracksCreated = true;
    app.midiPortsDirty = true;
    app.audio.player().locate(0);
    app.projectNotes = warnings;
    app.projectError.clear();
    for (const auto& w : warnings) app.log.warning("Project: " + w);
    importIntoProject(app);  // a project saved with outside takes (should not happen, but repairable)
    for (auto& t : app.tracks) refreshTakeLength(app, t);
    app.projectSavedText = minidaw::projectToJson(currentProject(app), folder);
    const unsigned int rate = app.audio.info().sampleRate;
    if (d.sampleRate && rate && d.sampleRate != rate) {
        const std::string note = "The project was saved at " + std::to_string(d.sampleRate) + " Hz; the stream runs at " +
                                 std::to_string(rate) + " Hz, so takes at the other rate are not played.";
        app.projectNotes.push_back(note);
        app.log.warning(note);
    }
    rememberProjectInConfig(app);
    app.log.info("Opened project " + minidaw::pathToUtf8(folder) + ": " + std::to_string(app.tracks.size()) +
                 " track(s)");
    return true;
}

// New: an empty project. Save as: the current tracks, with their takes copied in.
bool createProject(AppState& app, const std::filesystem::path& folder, bool keepTracks)
{
    if (app.session.recording()) {
        app.projectDialogError = "Stop the recording first.";
        return false;
    }
    std::string err;
    if (!minidaw::createProjectFolder(folder, err)) {
        app.projectDialogError = err;
        return false;
    }
    saveProjectNow(app);
    stopPlayback(app);
    app.projectFolder = folder;
    app.projectNotes.clear();
    app.projectError.clear();
    if (keepTracks) {
        importIntoProject(app);
    } else {
        app.tracks.clear();
        app.tracksCreated = false;  // the default track comes back
        app.midiPortsDirty = true;
        app.audio.player().locate(0);
    }
    app.projectSavedText.clear();
    saveProjectNow(app);
    rememberProjectInConfig(app);
    app.log.info(std::string(keepTracks ? "Saved as project " : "New project ") + minidaw::pathToUtf8(folder));
    return true;
}

void closeProject(AppState& app)
{
    if (app.session.recording() || app.projectFolder.empty()) return;
    saveProjectNow(app);
    stopPlayback(app);
    app.log.info("Closed project " + minidaw::pathToUtf8(app.projectFolder));
    app.projectFolder.clear();
    app.projectSavedText.clear();
    app.projectNotes.clear();
    app.projectError.clear();
    app.tracks.clear();
    app.tracksCreated = false;
    app.midiPortsDirty = true;
    app.audio.player().locate(0);
    rememberProjectInConfig(app);
}

void showProjectDialog(AppState& app, AppState::ProjectDialog kind)
{
    app.projectDialog = kind;
    app.projectDialogError.clear();
    app.projectNameInput = kind == AppState::ProjectDialog::SaveAs && !app.projectFolder.empty()
                               ? minidaw::pathToUtf8(app.projectFolder.filename()) + " copy"
                               : std::string();
    app.projectFolderInput = minidaw::pathToUtf8(projectsBase(app));
}

void drawProjectDialog(AppState& app)
{
    using D = AppState::ProjectDialog;
    if (app.projectDialog == D::None) return;
    const float em = ImGui::GetFontSize();
    const char* title = app.projectDialog == D::New    ? "New project###project"
                        : app.projectDialog == D::Open ? "Open project###project"
                                                       : "Save project as###project";
    bool open = true;
    // Grows with its content (the folder line, an error), so the buttons stay visible.
    ImGui::SetNextWindowSizeConstraints(ImVec2(46 * em, 0), ImVec2(46 * em, FLT_MAX));
    if (ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (app.session.recording()) ImGui::TextUnformatted("Recording: stop it first.");
        ImGui::BeginDisabled(app.session.recording());
        if (app.projectDialog == D::Open) {
            ImGui::TextWrapped("Projects in %s (click one), or type the folder of a project:",
                               minidaw::pathToUtf8(projectsBase(app)).c_str());
            const auto projects = minidaw::listProjects(projectsBase(app));
            if (ImGui::BeginChild("list", ImVec2(0, 8 * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders)) {
                if (projects.empty()) ImGui::TextUnformatted("No projects there yet.");
                for (const auto& p : projects) {
                    if (ImGui::Selectable(minidaw::pathToUtf8(p.filename()).c_str())) {
                        app.projectFolderInput = minidaw::pathToUtf8(p);
                    }
                }
            }
            ImGui::EndChild();
            ImGui::SetNextItemWidth(36 * em);
            ImGui::InputText("Folder", &app.projectFolderInput);
            if (ImGui::Button("Open") && openProject(app, minidaw::pathFromUtf8(app.projectFolderInput))) {
                app.projectDialog = D::None;
            }
        } else {
            ImGui::SetNextItemWidth(20 * em);
            ImGui::InputText("Name", &app.projectNameInput);
            ImGui::SetNextItemWidth(36 * em);
            ImGui::InputText("In folder", &app.projectFolderInput);
            const std::string name = minidaw::safeFileName(app.projectNameInput);
            const std::filesystem::path target = minidaw::pathFromUtf8(app.projectFolderInput) / minidaw::pathFromUtf8(name);
            if (!app.projectNameInput.empty()) ImGui::TextWrapped("Project folder: %s", minidaw::pathToUtf8(target).c_str());
            if (app.projectDialog == D::SaveAs) {
                ImGui::TextWrapped("The current tracks are saved there; their takes are copied into the new folder.");
            } else {
                ImGui::TextWrapped("Starts with an empty track list; new takes are recorded into the project folder.");
            }
            ImGui::BeginDisabled(app.projectNameInput.empty());
            if (ImGui::Button(app.projectDialog == D::New ? "Create" : "Save") &&
                createProject(app, target, app.projectDialog == D::SaveAs)) {
                app.projectDialog = D::None;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) app.projectDialog = D::None;
        if (!app.projectDialogError.empty()) ImGui::TextWrapped("%s", app.projectDialogError.c_str());
    }
    ImGui::End();
    if (!open) app.projectDialog = D::None;
}

// --- Playback (P8) ------------------------------------------------------------
minidaw::LatencyEstimate latencyNow(const AppState& app)
{
    const auto& info = app.audio.info();
    const auto reported = static_cast<unsigned int>(std::lround(info.reportedLatencyMs * info.sampleRate / 1000.0));
    return minidaw::estimateLatency(info.bufferFrames, reported, app.config.recording.latencyCorrectionMs,
                                    info.sampleRate);
}

// Track volume, mute and solo to both players, every frame (SPEC-PLAY-04).
void applyGains(AppState& app)
{
    const bool solo = minidaw::anySolo(app.tracks);
    for (const auto& t : app.tracks) {
        const float g = minidaw::trackGain(t, solo);
        app.audio.player().setGain(t.number, g, g);
        app.midiPlayer.setGain(t.number, g);
    }
}

// End of the last take on the timeline.
std::uint64_t songLength(const AppState& app)
{
    std::int64_t end = 0;
    for (const auto& t : app.tracks) {
        if (!t.take.empty()) end = std::max(end, t.takeStart + static_cast<std::int64_t>(t.takeFrames));
    }
    return static_cast<std::uint64_t>(std::max<std::int64_t>(end, 0));
}

// Play the takes from the play position (SPEC-PLAY-01..03). `forRecording`:
// started by Record; the armed tracks are left out (they are recorded now).
bool startPlayback(AppState& app, bool forRecording)
{
    std::uint64_t position = app.audio.player().position();
    // Play at (or past) the end starts from the beginning again (crosscheck #6, M2).
    if (!forRecording && position >= songLength(app)) position = 0;
    stopPlayback(app);
    app.audio.player().locate(position);
    const auto& info = app.audio.info();
    app.playWarnings.clear();
    if (!info.running) {
        app.playWarnings.push_back("The audio stream is not running.");
        return false;
    }
    std::vector<minidaw::PlaybackClip> clips;
    std::vector<minidaw::MidiPlaybackTrack> midiTracks;
    for (const auto& t : app.tracks) {
        if (t.take.empty() || (forRecording && t.armed)) continue;
        if (t.kind == minidaw::TrackKind::Audio) {
            clips.push_back({t.number, t.take, t.takeStart});
        } else if (!t.midiOut.empty()) {
            minidaw::SmfData d;
            std::string err;
            if (!minidaw::readSmf(t.take, d, err)) {
                app.playWarnings.push_back("Track " + std::to_string(t.number) + ": " + err);
                continue;
            }
            midiTracks.push_back({t.number, t.midiOut, minidaw::smfToFrames(d, info.sampleRate), t.takeStart});
        }
    }
    applyGains(app);
    const std::size_t audioStarted = app.audio.player().start(clips, position, info.sampleRate, app.playWarnings);
    const std::int64_t midiDelay = latencyNow(app).midiFrames;
    app.midiDelayFrames = midiDelay;
    app.midiPlayer.start(midiTracks, static_cast<std::int64_t>(position),
                         [&app, midiDelay]() -> std::optional<std::int64_t> {
                             // MIDI thread: the timeline as heard now (stopPlayback() stops this first).
                             const auto frame = app.audio.currentFrame();
                             if (!frame) return std::nullopt;
                             const auto t = app.audio.player().timelineAt(*frame);
                             if (!t) return std::nullopt;
                             return *t - midiDelay;
                         },
                         app.playWarnings);
    // Only takes that really play make it an overdub (a skipped clip does not, M6).
    app.overdub = audioStarted > 0 || app.midiPlayer.trackCount() > 0;
    if (forRecording) app.recordStartPosition = position;
    app.playbackForRecording = forRecording;
    for (const auto& w : app.playWarnings) app.log.warning("Playback: " + w);
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s from %.3f s: %zu audio and %zu MIDI take(s)",
                  forRecording ? "Recording timeline started" : "Playback started",
                  static_cast<double>(position) / info.sampleRate, audioStarted, app.midiPlayer.trackCount());
    app.log.info(buf);
    return true;
}

void togglePlay(AppState& app)
{
    if (app.session.recording()) return;  // Stop the recording with Ctrl+R
    if (app.audio.player().playing()) {
        stopPlayback(app);
        app.log.info("Playback stopped");
    } else {
        startPlayback(app, false);
    }
}

void returnToStart(AppState& app)
{
    if (app.session.recording()) return;
    const bool wasPlaying = app.audio.player().playing();
    stopPlayback(app);
    app.audio.player().locate(0);
    if (wasPlaying) startPlayback(app, false);
}

// Every frame: gains, end of playback, reader errors.
void updateTransport(AppState& app)
{
    applyGains(app);
    auto& player = app.audio.player();
    // The end of all takes, MIDI included (the player only knows the audio clips).
    // MIDI is sent `midiDelay` later than the audio position: let its tail out (M3).
    const std::uint64_t rate = app.audio.info().sampleRate;
    const std::uint64_t end = songLength(app) + static_cast<std::uint64_t>(std::max<std::int64_t>(app.midiDelayFrames, 0)) +
                              rate / 10;
    if (player.playing() && !app.playbackForRecording && player.position() >= end) {
        stopPlayback(app);
        app.log.info("Playback reached the end");
    }
    for (const auto& e : player.takeErrors()) {
        app.log.error(e);
        app.playWarnings.push_back(e);
    }
}

void toggleRecording(AppState& app)
{
    if (app.session.recording()) {
        const minidaw::SessionResult res = app.session.stop();
        app.lastTake = describeResult(res);
        app.log.info("Recording stopped");
        for (const auto& line : app.lastTake) {
            if (res.lostData()) app.log.warning("Take: " + line);
            else app.log.info("Take: " + line);
        }
        // Each armed track now plays its new take, placed where it was
        // recorded on the timeline; during an overdub moved earlier by the
        // latency estimate, so it lines up with what was heard (SPEC-PLAY-05).
        const auto timeline = res.startFrame ? app.audio.player().timelineAt(*res.startFrame) : std::nullopt;
        const auto lat = latencyNow(app);
        for (auto& t : app.tracks) {
            if (!t.armed) continue;
            const std::filesystem::path file = res.folder / minidaw::pathFromUtf8(minidaw::takeFileName(t));
            std::error_code ec;
            if (res.folder.empty() || !std::filesystem::exists(file, ec)) continue;
            t.take = file;
            t.takeStart = minidaw::placeRecordedTake(timeline, app.recordStartPosition, app.overdub,
                                                     t.kind == minidaw::TrackKind::Audio, lat);
            refreshTakeLength(app, t);
            char buf[200];
            std::snprintf(buf, sizeof buf, "Track %d plays %s from %.3f s%s", t.number,
                          minidaw::pathToUtf8(file.filename()).c_str(),
                          static_cast<double>(t.takeStart) / app.audio.info().sampleRate,
                          app.overdub ? " (overdub: moved earlier by the latency estimate)" : "");
            app.log.info(buf);
        }
        stopPlayback(app);
        return;
    }
    syncMidiPorts(app, true);
    app.recordError.clear();
    // The playback run gives the timeline (and plays the other takes: overdub).
    startPlayback(app, true);
    minidaw::RecordingSettings recSettings = app.config.recording;
    recSettings.folder = minidaw::pathToUtf8(activeTakesFolder(app));  // into the project, if one is open
    if (!app.session.start(app.tracks, recSettings, app.audio, app.midi, app.recordError)) {
        app.log.error("Not recording: " + app.recordError);
        stopPlayback(app);
        return;
    }
    app.log.info("Recording into " + minidaw::pathToUtf8(app.session.folder()));
    app.lastTake.clear();
    for (auto& m : app.inputMeters) m.clearClip();  // CLIP then means: during this take
    for (auto& m : app.outputMeters) m.clearClip();
}

std::string audioInputLabel(const std::vector<unsigned int>& ch)
{
    if (ch.size() == 2) return "In " + std::to_string(ch[0] + 1) + "+" + std::to_string(ch[1] + 1) + " (stereo)";
    if (ch.size() == 1) return "In " + std::to_string(ch[0] + 1) + " (mono)";
    return "(no input)";
}

void audioInputCombo(minidaw::Track& t, unsigned int inputs)
{
    if (!ImGui::BeginCombo("##in", audioInputLabel(t.channels).c_str())) return;
    for (unsigned int c = 0; c < inputs; ++c) {
        const std::vector<unsigned int> mono{c};
        if (ImGui::Selectable(audioInputLabel(mono).c_str(), t.channels == mono)) t.channels = mono;
    }
    for (unsigned int c = 0; c + 1 < inputs; ++c) {
        const std::vector<unsigned int> pair{c, c + 1};
        if (ImGui::Selectable(audioInputLabel(pair).c_str(), t.channels == pair)) t.channels = pair;
    }
    ImGui::EndCombo();
}

constexpr float kMidiChannelBoxEm = 6.0f;

bool midiPortCombo(minidaw::Track& t, const minidaw::DeviceScan& scan)
{
    bool changed = false;
    std::string preview = t.midiPort.empty() ? std::string("(choose a port)") : t.midiPort;
    if (!t.midiPort.empty() && std::none_of(scan.midiInputs.begin(), scan.midiInputs.end(),
                                            [&](const auto& p) { return p.name == t.midiPort; })) {
        preview += "  (not found)";
    }
    if (ImGui::BeginCombo("##port", preview.c_str())) {
        for (const auto& p : scan.midiInputs) {
            if (ImGui::Selectable(p.name.c_str(), t.midiPort == p.name)) {
                changed = t.midiPort != p.name;
                t.midiPort = p.name;
            }
        }
        if (scan.midiInputs.empty()) ImGui::TextUnformatted("No MIDI inputs found.");
        ImGui::EndCombo();
    } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !t.midiPort.empty()) {
        ImGui::SetTooltip("%s", preview.c_str());  // the full name when the box is too narrow
    }
    ImGui::SameLine();
    const std::string chText = t.midiChannel == 0 ? std::string("All ch") : "Ch " + std::to_string(t.midiChannel);
    ImGui::SetNextItemWidth(kMidiChannelBoxEm * ImGui::GetFontSize());
    const bool open = ImGui::BeginCombo("##ch", chText.c_str());
    if (!open && ImGui::IsItemHovered()) ImGui::SetTooltip("MIDI channel to record");
    if (open) {
        if (ImGui::Selectable("All channels", t.midiChannel == 0)) t.midiChannel = 0;
        for (int c = 1; c <= 16; ++c) {
            if (ImGui::Selectable(("Channel " + std::to_string(c)).c_str(), t.midiChannel == c)) t.midiChannel = c;
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string clockText(double seconds)
{
    if (seconds < 0) seconds = 0;
    const int m = static_cast<int>(seconds / 60);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%04.1f", m, seconds - 60.0 * m);
    return buf;
}

void drawTransport(AppState& app)
{
    ImGui::SeparatorText("Transport");
    const bool rec = app.session.recording();
    const bool running = app.audio.info().running;
    auto& player = app.audio.player();
    const bool playing = player.playing() && !app.playbackForRecording;
    const unsigned int rate = running ? app.audio.info().sampleRate : app.config.audio.sampleRate;
    const std::uint64_t length = songLength(app);

    ImGui::BeginDisabled(rec || !running);
    if (ImGui::Button("|< Start (Home)")) returnToStart(app);
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing && length == 0);
    if (ImGui::Button(playing ? "Stop (Space)" : "Play (Space)")) togglePlay(app);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!rec && !running);
    if (ImGui::Button(rec ? "Stop recording (Ctrl+R)" : "Record (Ctrl+R)")) toggleRecording(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const double pos = rate ? static_cast<double>(player.position()) / rate : 0.0;
    ImGui::Text("%s  %s / %s", rec ? "RECORDING" : playing ? "PLAYING" : "STOPPED", clockText(pos).c_str(),
                clockText(rate ? static_cast<double>(length) / rate : 0.0).c_str());
    if (!rec && !playing && length > 0 && rate > 0) {
        // Position slider, while stopped.
        float seconds = static_cast<float>(pos);
        ImGui::SetNextItemWidth(30 * ImGui::GetFontSize());
        const float rateF = static_cast<float>(rate);
        if (ImGui::SliderFloat("Position (s)", &seconds, 0.0f, static_cast<float>(length) / rateF, "%.1f s")) {
            player.locate(static_cast<std::uint64_t>(std::max(0.0f, seconds) * rateF));
        }
    }
    if (player.underrunFrames() > 0 && player.playing()) {
        ImGui::TextWrapped("Note: the disk was too slow for playback (%llu frames played as silence).",
                           static_cast<unsigned long long>(player.underrunFrames()));
    }
    for (const auto& w : app.playWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    if (rec) {
        if (app.overdub) ImGui::TextUnformatted("Overdub: the other takes play while the armed tracks record.");
        const double s = app.session.elapsedSeconds();
        const int m = static_cast<int>(s / 60);
        ImGui::Text("RECORDING  %02d:%04.1f   lost frames: %llu   MIDI messages: %zu (lost: %llu)", m, s - 60.0 * m,
                    static_cast<unsigned long long>(app.session.lostFrames()), app.session.midiMessages(),
                    static_cast<unsigned long long>(app.session.midiLost()));
        ImGui::TextWrapped("Into: %s", minidaw::pathToUtf8(app.session.folder()).c_str());
        const std::string err = app.session.diskError();
        if (!err.empty()) ImGui::TextWrapped("Error: %s", err.c_str());
        if (err != app.loggedRecordingError) {  // log it at once, not only at Stop (crosscheck #5, K3)
            if (!err.empty()) app.log.error("Recording: " + err);
            app.loggedRecordingError = err;
        }
    } else {
        if (ImGui::SmallButton("Recording settings...")) {
            app.pendingRecording = app.config.recording;
            app.showRecordingSettings = true;
        }
        ImGui::TextWrapped("Stopped. Takes go to %s", minidaw::pathToUtf8(activeTakesFolder(app)).c_str());
    }
    if (!app.recordError.empty()) ImGui::TextWrapped("Not recording: %s", app.recordError.c_str());
    if (!app.lastTake.empty()) {
        ImGui::TextUnformatted("Last take:");
        for (const auto& line : app.lastTake) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
}

void drawTracksPanel(AppState& app)
{
    ImGui::SeparatorText("Tracks");
    const bool rec = app.session.recording();
    const unsigned int inputs = app.audio.info().inputChannels;
    ImGui::BeginDisabled(rec);
    ImGui::BeginDisabled(inputs == 0);
    if (ImGui::Button("Add audio track")) {
        minidaw::Track t;
        t.number = minidaw::nextTrackNumber(app.tracks);
        t.name = "Audio " + std::to_string(t.number);
        t.channels = {0};
        app.tracks.push_back(t);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Add MIDI track")) {
        minidaw::Track t;
        t.number = minidaw::nextTrackNumber(app.tracks);
        t.kind = minidaw::TrackKind::Midi;
        t.name = "MIDI " + std::to_string(t.number);
        if (!app.scan.midiInputs.empty()) t.midiPort = app.scan.midiInputs.front().name;
        app.tracks.push_back(t);
    }
    ImGui::EndDisabled();
    if (inputs == 0) {
        ImGui::SameLine();
        ImGui::TextUnformatted("(audio tracks need an audio input device: Settings > Audio devices)");
    }
    if (app.tracks.empty()) ImGui::TextUnformatted("No tracks. Add an audio or MIDI track.");
    const bool anyArmed =
        std::any_of(app.tracks.begin(), app.tracks.end(), [](const minidaw::Track& t) { return t.armed; });
    if (!app.tracks.empty() && !anyArmed) {
        ImGui::TextUnformatted("Tick Arm on each track to record, then press Record (Ctrl+R). A MIDI track's port "
                               "opens when it is armed.");
    }

    const float em = ImGui::GetFontSize();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    int removeIndex = -1;
    const bool hasOutput = app.audio.info().running && app.audio.info().outputChannels > 0;
    bool monitoring = false;
    if (!app.tracks.empty() && ImGui::BeginTable("tracks", 9, flags)) {
        // Fixed widths, so the columns do not jump when the state text changes.
        const float box = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Arm", ImGuiTableColumnFlags_WidthFixed, box + ImGui::CalcTextSize("armed").x);
        ImGui::TableSetupColumn("Monitor", ImGuiTableColumnFlags_WidthFixed,
                                box + ImGui::CalcTextSize("on when armed").x);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Input");
        ImGui::TableSetupColumn("Level / activity");
        ImGui::TableSetupColumn("Volume, mute, solo");
        ImGui::TableSetupColumn("Take (plays)", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < app.tracks.size(); ++i) {
            minidaw::Track& t = app.tracks[i];
            ImGui::PushID(t.number);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", t.number);
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(rec);
            if (ImGui::Checkbox("##arm", &t.armed) && t.kind == minidaw::TrackKind::Midi) app.midiPortsDirty = true;
            ImGui::SameLine();
            ImGui::TextUnformatted(t.armed ? "armed" : "-");  // not by colour alone (SPEC-UI-07)
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (t.kind == minidaw::TrackKind::Audio) {  // can change while recording (SPEC-MON-01)
                ImGui::BeginDisabled(!hasOutput);
                ImGui::Checkbox("##mon", &t.monitor);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Hear this input on the output while the track is armed.\n"
                                      "Use headphones: with speakers a microphone can feed back.");
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                const char* state = !hasOutput   ? "no output"
                                    : !t.monitor ? "off"
                                    : t.armed    ? "on"
                                                 : "on when armed";
                ImGui::TextUnformatted(state);
                monitoring = monitoring || (hasOutput && t.monitor && t.armed);
            } else {
                ImGui::TextUnformatted("-");
            }
            ImGui::BeginDisabled(rec);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(8 * em);
            ImGui::InputText("##name", &t.name);
            ImGui::TableNextColumn();
            if (t.kind == minidaw::TrackKind::Audio) {
                ImGui::SetNextItemWidth(9.5f * em);
                audioInputCombo(t, inputs);
            } else {
                // The port list takes what the channel list leaves, so both stay visible.
                const float chWidth = kMidiChannelBoxEm * em + ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(std::max(9.5f * em, ImGui::GetContentRegionAvail().x - chWidth));
                if (midiPortCombo(t, app.scan) && t.armed) app.midiPortsDirty = true;
            }
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (t.kind == minidaw::TrackKind::Audio) {
                std::vector<minidaw::LevelMeter*> meters;
                for (unsigned int c : t.channels) {
                    if (c < inputs && c < app.inputMeters.size()) meters.push_back(&app.inputMeters[c]);
                }
                if (meters.empty()) {
                    ImGui::TextUnformatted("no input");
                } else {
                    drawMeter(app, meters, 7 * em);
                }
            } else if (t.armed && app.midi.isBlocked(t.midiPort)) {
                ImGui::TextUnformatted("CLOSED - arm off and on");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("One of several identical MIDI devices was unplugged; mini-daw cannot tell "
                                      "which.\nArm this track off and on to open the port again.");
                }
            } else if (t.armed && app.midi.isWanted(t.midiPort) && !app.midi.isOpen(t.midiPort)) {
                ImGui::TextUnformatted(app.midi.disconnects(t.midiPort) > 0 ? "DISCONNECTED - waiting for it"
                                                                           : "not connected - waiting for it");
            } else if (app.midi.isOpen(t.midiPort)) {
                // Activity as text (SPEC-MON-02, SPEC-UI-07): "receiving" for 0.3 s after a message.
                const auto it = app.midiActivity.find(t.midiPort);
                const bool active = it != app.midiActivity.end() && ImGui::GetTime() - it->second.changedAt < 0.3;
                ImGui::Text("%llu messages%s", static_cast<unsigned long long>(app.midi.messagesSeen(t.midiPort)),
                            active ? "  receiving" : "");
            } else {
                ImGui::TextUnformatted(t.armed ? "port not open" : "port opens when armed");
            }
            // Mix (SPEC-PLAY-04): may change at any time.
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(6.5f * em);
            ImGui::SliderFloat("##vol", &t.volumeDb, minidaw::kMinVolumeDb, minidaw::kMaxVolumeDb,
                               t.volumeDb <= minidaw::kMinVolumeDb ? "silent" : "%+.1f dB");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback volume. Ctrl+click to type a value.");
            ImGui::SameLine();
            ImGui::Checkbox("M##mute", &t.mute);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute: this track is not played.");
            ImGui::SameLine();
            ImGui::Checkbox("S##solo", &t.solo);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo: only soloed tracks are played.");
            // Take, and for MIDI the output it plays to (SPEC-PLAY-03).
            ImGui::TableNextColumn();
            if (t.kind == minidaw::TrackKind::Midi) {
                // Leave room for the take name beside it.
                const float nameW = t.take.empty() ? ImGui::CalcTextSize("-").x
                                                   : ImGui::CalcTextSize(minidaw::pathToUtf8(t.take.filename()).c_str()).x;
                ImGui::SetNextItemWidth(std::max(7 * em, ImGui::GetContentRegionAvail().x - nameW -
                                                             ImGui::GetStyle().ItemSpacing.x));
                const std::string out = t.midiOut.empty() ? std::string("(no MIDI output)") : t.midiOut;
                if (ImGui::BeginCombo("##out", out.c_str())) {
                    if (ImGui::Selectable("(no MIDI output)", t.midiOut.empty())) t.midiOut.clear();
                    for (const auto& name : app.midiPlayer.outputPorts()) {
                        if (ImGui::Selectable(name.c_str(), t.midiOut == name)) t.midiOut = name;
                    }
                    ImGui::EndCombo();
                } else if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("MIDI output this track plays to: %s", out.c_str());
                }
                ImGui::SameLine();
            }
            if (t.take.empty()) {
                ImGui::TextUnformatted("-");
            } else {
                ImGui::TextUnformatted(minidaw::pathToUtf8(t.take.filename()).c_str());
                if (ImGui::IsItemHovered()) {
                    const unsigned int r = app.audio.info().sampleRate ? app.audio.info().sampleRate : 48000;
                    ImGui::SetTooltip("%s\nstarts at %.3f s, %.1f s long", minidaw::pathToUtf8(t.take).c_str(),
                                      static_cast<double>(t.takeStart) / r, static_cast<double>(t.takeFrames) / r);
                }
            }
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(rec || app.audio.player().playing());  // its take would keep playing (M4)
            if (ImGui::SmallButton("X")) removeIndex = static_cast<int>(i);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Remove this track");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (monitoring) {
        const auto& info = app.audio.info();
        ImGui::TextWrapped("Monitoring: what you hear is delayed by at least two buffers (%.1f ms) plus the "
                           "driver's latency. Use headphones to avoid feedback.",
                           2000.0 * info.bufferFrames / info.sampleRate);
    }
    if (removeIndex >= 0) {
        if (app.tracks[removeIndex].kind == minidaw::TrackKind::Midi) app.midiPortsDirty = true;
        app.tracks.erase(app.tracks.begin() + removeIndex);
    }
    for (const auto& w : app.midiWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    for (const auto& m : app.midi.takeMessages()) {
        app.log.warning(m);
        app.midiWarnings.push_back(m);
        if (app.midiWarnings.size() > 4) app.midiWarnings.erase(app.midiWarnings.begin());
    }
}

void drawRecordingSettingsWindow(AppState& app)
{
    if (!app.showRecordingSettings) return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(44 * em, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Recording settings", &app.showRecordingSettings, ImGuiWindowFlags_NoCollapse)) {
        auto& p = app.pendingRecording;
        ImGui::SetNextItemWidth(30 * em);
        ImGui::InputText("Folder", &p.folder);
        ImGui::TextWrapped("Empty = %s", minidaw::pathToUtf8(minidaw::defaultRecordingFolder()).c_str());
        const char* timingText = p.midiTiming == minidaw::MidiTiming::Smpte ? "SMPTE, 1 ms ticks"
                                                                            : "960 PPQ at 120 BPM (default)";
        ImGui::SetNextItemWidth(20 * em);
        if (ImGui::BeginCombo("MIDI file timing", timingText)) {
            if (ImGui::Selectable("960 PPQ at 120 BPM (default)", p.midiTiming == minidaw::MidiTiming::Ppq))
                p.midiTiming = minidaw::MidiTiming::Ppq;
            if (ImGui::Selectable("SMPTE, 1 ms ticks", p.midiTiming == minidaw::MidiTiming::Smpte))
                p.midiTiming = minidaw::MidiTiming::Smpte;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Force takes to disk every second (survives power loss)", &p.syncToDisk);
        // Overdub calibration (SPEC-PLAY-05, crosscheck #6 M11).
        ImGui::SetNextItemWidth(8 * em);
        if (ImGui::InputInt("Overdub latency correction (ms)", &p.latencyCorrectionMs, 1, 10)) {
            p.latencyCorrectionMs = std::clamp(p.latencyCorrectionMs, -500, 500);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("New audio takes recorded while others play are moved earlier by the estimated\n"
                              "round-trip latency plus this value. Raise it if overdubs land late, lower it if\n"
                              "they land early. -500 to 500.");
        }
        ImGui::BeginDisabled(app.session.recording());
        if (ImGui::Button("Apply")) {
            app.config.recording = p;
            saveConfig(app);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close")) app.showRecordingSettings = false;
        if (app.session.recording()) ImGui::TextUnformatted("Changes apply after the current recording.");
    }
    ImGui::End();
}

void drawDevicesPanel(AppState& app)
{
    minidaw::DeviceScan& scan = app.scan;
    if (ImGui::Button("Rescan devices")) {
        scan = minidaw::scanDevices();
        app.log.info("Devices rescanned");
        logScan(app);
    }
    ImGui::SameLine();
    ImGui::Text("Audio API: %s", scan.audioApi.empty() ? "(none)" : scan.audioApi.c_str());

    ImGui::SeparatorText("Audio devices");
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("audio", 5, flags)) {
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Inputs");
        ImGui::TableSetupColumn("Outputs");
        ImGui::TableSetupColumn("Default");
        ImGui::TableHeadersRow();
        for (const auto& d : scan.audioDevices) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%u", d.id);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(d.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%u", d.inputChannels);
            ImGui::TableNextColumn(); ImGui::Text("%u", d.outputChannels);
            ImGui::TableNextColumn();
            ImGui::Text("%s%s", d.isDefaultInput ? "input " : "", d.isDefaultOutput ? "output" : "");
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("MIDI inputs");
    for (const auto& p : scan.midiInputs) ImGui::BulletText("[%u] %s", p.index, p.name.c_str());
    if (scan.midiInputs.empty()) ImGui::TextUnformatted("(none)");

    ImGui::SeparatorText("MIDI outputs");
    for (const auto& p : scan.midiOutputs) ImGui::BulletText("[%u] %s", p.index, p.name.c_str());
    if (scan.midiOutputs.empty()) ImGui::TextUnformatted("(none)");

    if (!scan.warnings.empty()) {
        ImGui::SeparatorText("Device warnings");
        for (const auto& w : scan.warnings) ImGui::BulletText("%s", w.c_str());
    }
}

// Open a folder in the system's file manager.
bool openFolder(const std::filesystem::path& folder)
{
#if defined(_WIN32)
    const auto r = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return r > 32;
#else
#if defined(__APPLE__)
    const char* tool = "open";
#else
    const char* tool = "xdg-open";
#endif
    // Do not wait for it: on some desktops xdg-open stays until the file
    // manager closes (crosscheck #5, K6). Children from earlier clicks are
    // reaped here without blocking.
    static std::vector<pid_t> children;
    std::erase_if(children, [](pid_t p) { return waitpid(p, nullptr, WNOHANG) != 0; });
    const std::string dir = folder.string();
    char* args[] = {const_cast<char*>(tool), const_cast<char*>(dir.c_str()), nullptr};
    pid_t pid = 0;
    if (posix_spawnp(&pid, tool, nullptr, nullptr, args, environ) != 0) return false;  // e.g. no xdg-open
    children.push_back(pid);
    return true;
#endif
}

// View > Log (SPEC-LOG-01): the latest lines of this run, level as text.
void drawLogWindow(AppState& app)
{
    if (!app.showLog) return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(60 * em, 24 * em), ImGuiCond_Appearing);
    if (ImGui::Begin("Log", &app.showLog)) {
        const std::filesystem::path file = app.log.file();
        if (file.empty()) {
            ImGui::TextWrapped("No log file: %s", app.logError.c_str());
        } else {
            ImGui::TextWrapped("File: %s (the previous run: %s)", minidaw::pathToUtf8(file).c_str(),
                               minidaw::pathToUtf8(minidaw::Log::oldFileFor(file).filename()).c_str());
        }
        ImGui::BeginDisabled(file.empty());
        if (ImGui::Button("Open log folder")) {
            if (!openFolder(file.parent_path())) app.log.warning("The log folder could not be opened.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const std::vector<minidaw::LogEntry> lines = app.log.recent();
        if (ImGui::Button("Copy all")) {
            std::string all;
            for (const auto& e : lines) all += minidaw::Log::formatLine(e);
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::SameLine();
        ImGui::Checkbox("Only warnings and errors", &app.logOnlyProblems);
        ImGui::Separator();
        if (ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
            for (const auto& e : lines) {
                if (app.logOnlyProblems && e.level == minidaw::LogLevel::Info) continue;
                const std::string line = minidaw::Log::formatLine(e);
                ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size() - 1);  // without the newline
            }
            const std::uint64_t count = app.log.lineCount();
            if (count != app.logLinesShown && atBottom) ImGui::SetScrollHereY(1.0f);  // follow new lines
            app.logLinesShown = count;
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// File > Open take folder (P8): the take folders in the recording folder,
// newest first, or any folder typed in. Opening replaces the track list.
void openTakeFolder(AppState& app, const std::filesystem::path& folder)
{
    if (app.session.recording()) {  // the recording owns the track list and the timeline (M1)
        app.takeBrowserError = "Stop the recording first.";
        return;
    }
    std::string err;
    std::vector<minidaw::Track> tracks = minidaw::tracksFromTakeFolder(folder, err);
    if (tracks.empty()) {
        app.takeBrowserError = err;
        app.log.warning("Open take folder: " + err);
        return;
    }
    stopPlayback(app);
    app.tracks = std::move(tracks);
    for (auto& t : app.tracks) refreshTakeLength(app, t);
    app.tracksCreated = true;     // no default track on top of these
    app.midiPortsDirty = true;    // MIDI tracks of the old list may have had ports open
    app.audio.player().locate(0);
    app.showTakeBrowser = false;
    importIntoProject(app);  // a project keeps its own copy of the takes
    for (auto& t : app.tracks) refreshTakeLength(app, t);
    app.log.info("Opened take folder " + minidaw::pathToUtf8(folder) + ": " + std::to_string(app.tracks.size()) +
                 " track(s)");
}

void drawTakeBrowser(AppState& app)
{
    if (!app.showTakeBrowser) return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(44 * em, 26 * em), ImGuiCond_Appearing);
    if (ImGui::Begin("Open take folder", &app.showTakeBrowser)) {
        ImGui::TextWrapped("Takes in %s (newest first). Opening a take replaces the track list.",
                           minidaw::pathToUtf8(activeTakesFolder(app)).c_str());
        const bool rec = app.session.recording();
        if (rec) ImGui::TextUnformatted("Recording: stop it before opening a take folder.");
        ImGui::BeginDisabled(rec);
        if (ImGui::BeginChild("folders", ImVec2(0, -4 * ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders)) {
            if (app.takeFolders.empty()) ImGui::TextUnformatted("No take folders found.");
            for (const auto& f : app.takeFolders) {
                if (ImGui::Selectable(minidaw::pathToUtf8(f.filename()).c_str(), false,
                                      ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    openTakeFolder(app, f);
                } else if (ImGui::IsItemClicked()) {
                    app.takeFolderInput = minidaw::pathToUtf8(f);
                }
            }
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth(34 * em);
        ImGui::InputText("Folder", &app.takeFolderInput);
        if (ImGui::Button("Open") && !app.takeFolderInput.empty()) {
            openTakeFolder(app, minidaw::pathFromUtf8(app.takeFolderInput));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) app.showTakeBrowser = false;
        ImGui::SameLine();
        ImGui::TextUnformatted("(double-click a take to open it)");
        if (!app.takeBrowserError.empty()) ImGui::TextWrapped("%s", app.takeBrowserError.c_str());
    }
    ImGui::End();
}

void drawStatusLine(const AppState& app)
{
    ImGui::Separator();
    const auto lowest = minidaw::lowestContrast(app.palette);
    ImGui::TextWrapped("Contrast: %s, lowest pair %.2f:1. Ctrl+Shift+Up/Down to change. Text: %d px (Ctrl+= / Ctrl+-).",
                describeContrast(app.config.contrast).c_str(), lowest.ratio, static_cast<int>(app.config.fontSize));
    if (app.projectFolder.empty()) {
        ImGui::TextUnformatted("Project: none (File > New project / Save project as to keep this session)");
    } else {
        ImGui::Text("Project: %s (%s)", minidaw::pathToUtf8(app.projectFolder).c_str(),
                    app.projectError.empty() ? "saved automatically" : "NOT SAVED - see the log");
    }
    for (const auto& n : app.projectNotes) ImGui::TextWrapped("Project note: %s", n.c_str());
    ImGui::Text("Config: %s", minidaw::pathToUtf8(app.configPath).c_str());
    ImGui::Text("Graphics: %s.   Log: %llu warning(s), %llu error(s) - View > Log (Ctrl+L)", app.rendererName.c_str(),
                static_cast<unsigned long long>(app.log.warningCount()),
                static_cast<unsigned long long>(app.log.errorCount()));
    for (const auto& w : app.configWarnings) ImGui::TextWrapped("Config warning: %s", w.c_str());
    if (!app.saveError.empty()) ImGui::TextWrapped("Could not save config: %s", app.saveError.c_str());
    if (!app.unresolved.empty()) {
        ImGui::Text("Warning: %zu colour pair(s) could not reach the minimum.", app.unresolved.size());
    }
}

std::string g_lastGlfwError;

int runGui(AppState& app)
{
    glfwSetErrorCallback([](int code, const char* text) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, text);
        g_lastGlfwError = text;
    });
    if (!glfwInit()) {
        const std::string fatal = "The window system could not be initialised: " + g_lastGlfwError;
        app.log.error(fatal);
        minidaw::app::showFatalError(fatal);
        return 1;
    }
    minidaw::app::rendererWindowHints();

    const std::string title = std::string(minidaw::kProductName) + " " + minidaw::kVersion;
    GLFWwindow* window = glfwCreateWindow(1280, 760, title.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        const std::string fatal = "The main window could not be created: " + g_lastGlfwError;
        app.log.error(fatal);
        minidaw::app::showFatalError(fatal);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // window layout is not saved
    ImGui::GetIO().Fonts->AddFontDefaultVector();  // scalable, so the text size can change

    std::string error;
    if (!minidaw::app::rendererInit(window, error)) {
        const std::string fatal = error;
        app.log.error(fatal);
        minidaw::app::showFatalError(fatal);
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    app.rendererName = minidaw::app::rendererName();
    app.log.info("Graphics: " + app.rendererName);

    setContrast(app, app.config.contrast, false);
    setFontSize(app, app.config.fontSize, false);
    app.scan = minidaw::scanDevices();
    logScan(app);
    applyAudioSettings(app, app.config.audio, false);
    if (!app.config.project.last.empty()) {  // reopen the last project (SPEC-PRJ-02a)
        const std::filesystem::path last = minidaw::pathFromUtf8(app.config.project.last);
        if (minidaw::isProjectFolder(last)) {
            openProject(app, last);
        } else {
            const std::string note = "The last project " + app.config.project.last + " was not found.";
            app.projectNotes.push_back(note);
            app.log.warning(note);
        }
    }

    bool quit = false;
    while (!quit && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            glfwWaitEventsTimeout(0.1);  // minimised: don't render
            continue;
        }
        minidaw::app::rendererNewFrame(window);
        ImGui::NewFrame();

        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_UpArrow)) stepContrast(app, +1);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_DownArrow)) stepContrast(app, -1);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal) ||
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd))
            setFontSize(app, app.config.fontSize + 2, true);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus) ||
            ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract))
            setFontSize(app, app.config.fontSize - 2, true);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0)) setFontSize(app, minidaw::kDefaultFontSize, true);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_R)) toggleRecording(app);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) app.showLog = !app.showLog;
        if (!ImGui::GetIO().WantTextInput) {  // not while typing a name
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) togglePlay(app);
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) returnToStart(app);
        }
        ensureDefaultTracks(app);
        syncMidiPorts(app);
        updateMeters(app);
        updateTransport(app);
        autosaveProject(app);
        {
            const std::string t = std::string(minidaw::kProductName) + " " + minidaw::kVersion +
                                  (app.projectFolder.empty() ? std::string() : " - " + minidaw::pathToUtf8(app.projectFolder.filename()));
            if (t != app.windowTitle) {
                glfwSetWindowTitle(window, t.c_str());
                app.windowTitle = t;
            }
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
        drawMenuBar(app, quit);
        const float statusHeight = ImGui::GetTextLineHeightWithSpacing() * 5.5f;
        if (ImGui::BeginChild("content", ImVec2(0, -statusHeight))) {
            drawAudioStatus(app);
            drawTransport(app);
            drawTracksPanel(app);
            if (ImGui::CollapsingHeader("Devices")) drawDevicesPanel(app);
        }
        ImGui::EndChild();
        drawStatusLine(app);
        ImGui::End();

        drawAudioSettingsWindow(app);
        drawRecordingSettingsWindow(app);
        drawCustomContrastWindow(app);
        drawContrastReportWindow(app);
        drawLogWindow(app);
        drawTakeBrowser(app);
        drawProjectDialog(app);

        ImGui::Render();
        const auto& bg = app.palette[minidaw::Role::Background];
        const float clear[4] = {static_cast<float>(bg.r), static_cast<float>(bg.g), static_cast<float>(bg.b), 1.0f};
        minidaw::app::rendererRender(window, clear);
    }

    if (app.session.recording()) app.session.stop();  // finish the files before the stream closes
    stopPlayback(app);
    saveProjectNow(app);
    app.midi.closeAll();
    app.audio.close();
    minidaw::app::rendererShutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// A whole number of seconds in [lo, hi], or -1 (crosscheck #4, J12).
int parseSeconds(const char* text, int lo, int hi)
{
    char* end = nullptr;
    const long v = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < lo || v > hi) return -1;
    return static_cast<int>(v);
}

void printUsage(const char* argv0)
{
    std::printf("usage: %s [--config <file>] [--list-devices | --audio-test <seconds> [--monitor] |\n"
                "        --record-test <seconds> <folder> | --contrast-report | --version | --help]\n",
                argv0);
}

} // namespace

int main(int argc, char** argv)
{
    AppState app;
    app.configPath = minidaw::defaultConfigPath();
    enum class Action { Gui, ListDevices, AudioTest, RecordTest, ContrastReport } action = Action::Gui;
    int audioTestSeconds = 3;
    bool audioTestMonitor = false;
    std::filesystem::path recordFolder;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--version") == 0) {
            std::printf("%s %s\n", minidaw::kProductName, minidaw::kVersion);
            return 0;
        }
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--list-devices") == 0) {
            action = Action::ListDevices;
        } else if (std::strcmp(arg, "--contrast-report") == 0) {
            action = Action::ContrastReport;
        } else if (std::strcmp(arg, "--audio-test") == 0 && i + 1 < argc) {
            action = Action::AudioTest;
            audioTestSeconds = parseSeconds(argv[++i], 1, 600);
            if (audioTestSeconds < 0) {
                std::fprintf(stderr, "--audio-test needs a number of seconds from 1 to 600, not '%s'\n", argv[i]);
                return 2;
            }
        } else if (std::strcmp(arg, "--record-test") == 0 && i + 2 < argc) {
            action = Action::RecordTest;
            audioTestSeconds = parseSeconds(argv[++i], 1, 3600);
            if (audioTestSeconds < 0) {
                std::fprintf(stderr, "--record-test needs a number of seconds from 1 to 3600, not '%s'\n", argv[i]);
                return 2;
            }
            recordFolder = std::filesystem::path(argv[++i]);  // native encoding, as argv is
        } else if (std::strcmp(arg, "--monitor") == 0) {
            audioTestMonitor = true;
        } else if (std::strcmp(arg, "--config") == 0 && i + 1 < argc) {
            app.configPath = argv[++i];
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", arg);
            return 2;
        }
    }

    if (audioTestMonitor && action != Action::AudioTest) {
        std::fprintf(stderr, "--monitor only works with --audio-test\n");
        return 2;
    }

    auto loaded = minidaw::loadConfigFile(app.configPath);
    app.config = loaded.config;
    app.configWarnings = loaded.warnings;
    for (const auto& w : app.configWarnings) std::fprintf(stderr, "config warning: %s\n", w.c_str());

    switch (action) {
    case Action::ListDevices:
        printDevices(minidaw::scanDevices());
        return 0;
    case Action::AudioTest:
        openLog(app, "--audio-test");
        return runAudioTest(app, audioTestSeconds, audioTestMonitor);
    case Action::RecordTest:
        openLog(app, "--record-test");
        return runRecordTest(app, audioTestSeconds, recordFolder);
    case Action::ContrastReport: {
        auto result = buildPalette(app.config.contrast);
        app.palette = result.palette;
        app.unresolved = result.unresolved;
        return printContrastReport(app);
    }
    case Action::Gui:
        break;
    }
    openLog(app, "GUI");
    const int rc = runGui(app);
    app.log.info("mini-daw closed");
    return rc;
}
