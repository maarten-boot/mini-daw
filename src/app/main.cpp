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
#include "minidaw/midi_input.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/recording_session.hpp"
#include "minidaw/track.hpp"
#include "minidaw/theme.hpp"
#include "minidaw/version.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
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
    std::array<float, minidaw::kMaxInputChannels> levelHold{};  // shown peak per channel
    double levelHoldStart = 0.0;

    // Tracks and recording (P6). Declared after `audio`, so they are destroyed first.
    std::vector<minidaw::Track> tracks;
    bool tracksCreated = false;
    minidaw::MidiInputs midi;
    bool midiPortsDirty = true;            // armed MIDI tracks changed: reopen ports
    double midiRetryAt = 0.0;              // when to retry a port that did not open (0 = no retry due)
    std::vector<std::string> midiWarnings;
    minidaw::RecordingSession session;
    std::string recordError;
    std::vector<std::string> lastTake;     // summary of the last recording
    bool showRecordingSettings = false;
    minidaw::RecordingSettings pendingRecording;
};

void saveConfig(AppState& app)
{
    app.saveError = minidaw::saveConfigFile(app.configPath, app.config);
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
void applyAudioSettings(AppState& app, const minidaw::AudioSettings& settings, bool save)
{
    if (app.session.recording()) {
        app.audioError = "Stop the recording before changing the audio devices.";
        return;
    }
    const minidaw::AudioSettings previous = app.config.audio;
    minidaw::OpenResult r = app.audio.open(settings, app.scan);
    app.audioWarnings = r.warnings;
    app.audioError = r.error;
    if (r.error.empty()) {
        app.config.audio = settings;
        if (save) saveConfig(app);
    } else if (!(previous == settings)) {
        minidaw::OpenResult back = app.audio.open(previous, app.scan);
        if (back.error.empty()) {
            app.audioWarnings.push_back("The previous audio settings are active again.");
        }
    }
    app.levelHold.fill(0.0f);
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
int runAudioTest(AppState& app, int seconds)
{
    app.scan = minidaw::scanDevices();
    const auto& s = app.config.audio;
    std::printf("Requested: input %s, output %s, %u Hz, %u frames\n", s.inputDevice.c_str(),
                s.outputDevice.c_str(), s.sampleRate, s.bufferFrames);
    minidaw::OpenResult r = app.audio.open(s, app.scan);
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
    // Measure from a fixed starting point: the count already includes the callbacks
    // made while open() waited, and Windows' sleep_for() overshoots, so use real time.
    const std::uint64_t startCallbacks = app.audio.callbacks();
    const auto startTime = std::chrono::steady_clock::now();
    for (int t = 0; t < seconds * 10; ++t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
            peak[ch] = std::max(peak[ch], app.audio.takeInputPeak(ch));
        }
    }
    const std::uint64_t callbacks = app.audio.callbacks() - startCallbacks;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    const double expected = info.bufferFrames ? elapsed * info.sampleRate / info.bufferFrames : 0.0;
    for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
        std::printf("%s in %2u: %-10s", ch % 4 == 0 ? "  peak" : "", ch + 1, minidaw::formatDbfs(peak[ch]).c_str());
        if (ch % 4 == 3 || ch + 1 == info.inputChannels) std::printf("\n");
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
    }
    if (!res.audio.error.empty()) lines.push_back("error: " + res.audio.error);
    if (!res.error.empty()) lines.push_back("error: " + res.error);
    return lines;
}

// --record-test: record input 1 (and 2) and every MIDI input into a take
// folder in `folder` (T-REC-10, SPEC-REC-06), through the same recording
// session as the GUI.
int runRecordTest(AppState& app, int seconds, const std::filesystem::path& folder)
{
    app.scan = minidaw::scanDevices();
    for (const auto& w : app.scan.warnings) std::printf("warning: %s\n", w.c_str());
    minidaw::OpenResult r = app.audio.open(app.config.audio, app.scan);
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
    for (const auto& w : app.midi.setPorts(ports)) std::printf("warning: %s\n", w.c_str());
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
    for (int t = 0; t < seconds * 10; ++t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!app.session.diskError().empty()) break;
    }
    const minidaw::SessionResult res = app.session.stop();
    const std::uint64_t xruns = app.audio.xruns();
    app.midi.closeAll();
    app.audio.close();

    for (const auto& line : describeResult(res)) std::printf("%s\n", line.c_str());
    std::printf("dropouts (xruns): %llu\n", static_cast<unsigned long long>(xruns));
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

void drawMenuBar(AppState& app, bool& quit)
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
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
        if (ImGui::Button("Rescan devices")) app.scan = minidaw::scanDevices();
        ImGui::SameLine();
        if (ImGui::Button("Close")) app.showAudioSettings = false;
        if (!app.audioError.empty()) ImGui::TextWrapped("Error: %s", app.audioError.c_str());
        for (const auto& w : app.audioWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    }
    ImGui::End();
}

void drawAudioStatus(AppState& app)
{
    ImGui::SeparatorText("Audio stream");
    if (ImGui::Button("Audio settings...")) openAudioSettings(app);
    const auto& info = app.audio.info();
    if (!info.running) {
        ImGui::TextWrapped("Stopped. %s", app.audioError.c_str());
    } else {
        ImGui::Text("Running: %s, %u Hz, %u frames (%.1f ms per buffer), reported latency %.1f ms", info.api.c_str(),
                    info.sampleRate, info.bufferFrames, 1000.0 * info.bufferFrames / info.sampleRate,
                    info.reportedLatencyMs);
        ImGui::Text("Input:  %s (%u ch)", info.inputName.empty() ? "(none)" : info.inputName.c_str(),
                    info.inputChannels);
        ImGui::Text("Output: %s (%u ch)", info.outputName.empty() ? "(none)" : info.outputName.c_str(),
                    info.outputChannels);
        ImGui::Text("Dropouts (xruns): %llu", static_cast<unsigned long long>(app.audio.xruns()));

        // Input levels as numbers (SPEC-UI-07: not colour alone). Peak held for 0.3 s.
        const double now = ImGui::GetTime();
        const bool refresh = now - app.levelHoldStart > 0.3;
        for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
            const float p = app.audio.takeInputPeak(ch);
            app.levelHold[ch] = refresh ? p : std::max(app.levelHold[ch], p);
        }
        if (refresh) app.levelHoldStart = now;
        // Each track shows its own level; the full list is folded away (crosscheck #2, G3).
        const std::string header = "Input levels (" + std::to_string(info.inputChannels) + " channels)###levels";
        if (info.inputChannels > 0 && ImGui::TreeNode(header.c_str())) {
            for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
                if (ch % 4 != 0) ImGui::SameLine(static_cast<float>(ch % 4) * 12 * ImGui::GetFontSize());
                ImGui::Text("In %u: %s", ch + 1, minidaw::formatDbfs(app.levelHold[ch]).c_str());
            }
            ImGui::TreePop();
        }
    }
    for (const auto& w : app.audioWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    for (const auto& m : app.audio.takeDriverMessages()) app.audioWarnings.push_back(m);
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
void syncMidiPorts(AppState& app, bool force = false)
{
    if (app.session.recording()) return;
    const double now = ImGui::GetTime();
    const bool retryDue = app.midiRetryAt != 0.0 && now >= app.midiRetryAt;
    if (!force && !app.midiPortsDirty && !retryDue) return;
    std::vector<std::string> ports;
    for (const auto& t : app.tracks) {
        if (t.kind == minidaw::TrackKind::Midi && t.armed && !t.midiPort.empty()) ports.push_back(t.midiPort);
    }
    app.midiWarnings = app.midi.setPorts(ports);
    app.midiPortsDirty = false;
    const bool allOpen =
        std::all_of(ports.begin(), ports.end(), [&](const std::string& p) { return app.midi.isOpen(p); });
    app.midiRetryAt = allOpen ? 0.0 : now + 2.0;
}

void toggleRecording(AppState& app)
{
    if (app.session.recording()) {
        app.lastTake = describeResult(app.session.stop());
        return;
    }
    syncMidiPorts(app, true);
    app.recordError.clear();
    if (!app.session.start(app.tracks, app.config.recording, app.audio, app.midi, app.recordError)) return;
    app.lastTake.clear();
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

bool midiPortCombo(minidaw::Track& t, const minidaw::DeviceScan& scan)
{
    bool changed = false;
    const std::string preview = t.midiPort.empty() ? std::string("(choose a port)") : t.midiPort;
    if (ImGui::BeginCombo("##port", preview.c_str())) {
        for (const auto& p : scan.midiInputs) {
            if (ImGui::Selectable(p.name.c_str(), t.midiPort == p.name)) {
                changed = t.midiPort != p.name;
                t.midiPort = p.name;
            }
        }
        if (scan.midiInputs.empty()) ImGui::TextUnformatted("No MIDI inputs found.");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const std::string chText = t.midiChannel == 0 ? std::string("All channels") : "Channel " + std::to_string(t.midiChannel);
    ImGui::SetNextItemWidth(9 * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##ch", chText.c_str())) {
        if (ImGui::Selectable("All channels", t.midiChannel == 0)) t.midiChannel = 0;
        for (int c = 1; c <= 16; ++c) {
            if (ImGui::Selectable(("Channel " + std::to_string(c)).c_str(), t.midiChannel == c)) t.midiChannel = c;
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawTransport(AppState& app)
{
    ImGui::SeparatorText("Recording");
    const bool rec = app.session.recording();
    ImGui::BeginDisabled(!rec && !app.audio.info().running);
    if (ImGui::Button(rec ? "Stop (Ctrl+R)" : "Record (Ctrl+R)")) toggleRecording(app);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (rec) {
        const double s = app.session.elapsedSeconds();
        const int m = static_cast<int>(s / 60);
        ImGui::Text("RECORDING  %02d:%04.1f   lost frames: %llu   MIDI messages: %zu (lost: %llu)", m, s - 60.0 * m,
                    static_cast<unsigned long long>(app.session.lostFrames()), app.session.midiMessages(),
                    static_cast<unsigned long long>(app.session.midiLost()));
        ImGui::TextWrapped("Into: %s", minidaw::pathToUtf8(app.session.folder()).c_str());
        const std::string err = app.session.diskError();
        if (!err.empty()) ImGui::TextWrapped("Error: %s", err.c_str());
    } else {
        if (ImGui::SmallButton("Recording settings...")) {
            app.pendingRecording = app.config.recording;
            app.showRecordingSettings = true;
        }
        ImGui::TextWrapped("Stopped. Takes go to %s", minidaw::pathToUtf8(minidaw::recordingFolder(app.config.recording)).c_str());
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
    if (app.tracks.empty()) ImGui::TextUnformatted("No tracks. Add an audio or MIDI track.");

    const float em = ImGui::GetFontSize();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    int removeIndex = -1;
    if (!app.tracks.empty() && ImGui::BeginTable("tracks", 6, flags)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Arm");
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Level / activity");
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
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(12 * em);
            ImGui::InputText("##name", &t.name);
            ImGui::TableNextColumn();
            if (t.kind == minidaw::TrackKind::Audio) {
                ImGui::SetNextItemWidth(14 * em);
                audioInputCombo(t, inputs);
            } else {
                ImGui::SetNextItemWidth(18 * em);
                if (midiPortCombo(t, app.scan) && t.armed) app.midiPortsDirty = true;
            }
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (t.kind == minidaw::TrackKind::Audio) {
                float peak = 0.0f;
                for (unsigned int c : t.channels) {
                    if (c < app.levelHold.size()) peak = std::max(peak, app.levelHold[c]);
                }
                ImGui::TextUnformatted(minidaw::formatDbfs(peak).c_str());
            } else if (app.midi.isOpen(t.midiPort)) {
                ImGui::Text("%llu messages", static_cast<unsigned long long>(app.midi.messagesSeen(t.midiPort)));
            } else {
                ImGui::TextUnformatted(t.armed ? "port not open" : "port opens when armed");
            }
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(rec);
            if (ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0) {
        if (app.tracks[removeIndex].kind == minidaw::TrackKind::Midi) app.midiPortsDirty = true;
        app.tracks.erase(app.tracks.begin() + removeIndex);
    }
    for (const auto& w : app.midiWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    for (const auto& m : app.midi.takeMessages()) app.midiWarnings.push_back(m);
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

void drawDevicesPanel(minidaw::DeviceScan& scan)
{
    if (ImGui::Button("Rescan devices")) {
        scan = minidaw::scanDevices();
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

void drawStatusLine(const AppState& app)
{
    ImGui::Separator();
    const auto lowest = minidaw::lowestContrast(app.palette);
    ImGui::TextWrapped("Contrast: %s, lowest pair %.2f:1. Ctrl+Shift+Up/Down to change. Text: %d px (Ctrl+= / Ctrl+-).",
                describeContrast(app.config.contrast).c_str(), lowest.ratio, static_cast<int>(app.config.fontSize));
    ImGui::Text("Config: %s", app.configPath.string().c_str());
    ImGui::Text("Graphics: %s", app.rendererName.c_str());
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
        minidaw::app::showFatalError("The window system could not be initialised: " + g_lastGlfwError);
        return 1;
    }
    minidaw::app::rendererWindowHints();

    const std::string title = std::string(minidaw::kProductName) + " " + minidaw::kVersion;
    GLFWwindow* window = glfwCreateWindow(1100, 700, title.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        minidaw::app::showFatalError("The main window could not be created: " + g_lastGlfwError);
        glfwTerminate();
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // window layout is not saved
    ImGui::GetIO().Fonts->AddFontDefaultVector();  // scalable, so the text size can change

    std::string error;
    if (!minidaw::app::rendererInit(window, error)) {
        minidaw::app::showFatalError(error);
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    app.rendererName = minidaw::app::rendererName();

    setContrast(app, app.config.contrast, false);
    setFontSize(app, app.config.fontSize, false);
    app.scan = minidaw::scanDevices();
    applyAudioSettings(app, app.config.audio, false);

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
        ensureDefaultTracks(app);
        syncMidiPorts(app);

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
            if (ImGui::CollapsingHeader("Devices")) drawDevicesPanel(app.scan);
        }
        ImGui::EndChild();
        drawStatusLine(app);
        ImGui::End();

        drawAudioSettingsWindow(app);
        drawRecordingSettingsWindow(app);
        drawCustomContrastWindow(app);
        drawContrastReportWindow(app);

        ImGui::Render();
        const auto& bg = app.palette[minidaw::Role::Background];
        const float clear[4] = {static_cast<float>(bg.r), static_cast<float>(bg.g), static_cast<float>(bg.b), 1.0f};
        minidaw::app::rendererRender(window, clear);
    }

    if (app.session.recording()) app.session.stop();  // finish the files before the stream closes
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
    std::printf("usage: %s [--config <file>] [--list-devices | --audio-test <seconds> |\n"
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
        } else if (std::strcmp(arg, "--config") == 0 && i + 1 < argc) {
            app.configPath = argv[++i];
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", arg);
            return 2;
        }
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
        return runAudioTest(app, audioTestSeconds);
    case Action::RecordTest:
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
    return runGui(app);
}
