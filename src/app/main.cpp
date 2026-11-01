// mini-daw — application entry point.
//
// Command line:
//   mini-daw                       open the GUI
//   mini-daw --config <file>       use this config file instead of the default one
//   mini-daw --list-devices        print audio devices and MIDI ports, then exit
//   mini-daw --audio-test <sec>    open the configured audio devices for <sec> seconds,
//                                  print the actual settings and input levels, then exit
//   mini-daw --record-test <sec> <folder>
//                                  record input channel 1 (and 2, if present) for <sec>
//                                  seconds to a 24-bit WAV file in <folder>, then exit
//   mini-daw --contrast-report     print every colour pair and its contrast, then exit
//   mini-daw --version             print the version, then exit

#include "imgui_theme.hpp"
#include "renderer.hpp"

#include "minidaw/audio_stream.hpp"
#include "minidaw/config.hpp"
#include "minidaw/devices.hpp"
#include "minidaw/theme.hpp"
#include "minidaw/version.hpp"

#include <imgui.h>

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

// "take-20261101-223900.wav" (local time), so takes sort by date.
std::string takeFileName()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "take-%Y%m%d-%H%M%S.wav", &local);
    return buf;
}

// --record-test: record the first one or two input channels to a WAV file
// (T-REC-10; exercises the recording engine on a real device before P6).
int runRecordTest(AppState& app, int seconds, const std::filesystem::path& folder)
{
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        std::printf("error: cannot create folder %s: %s\n", folder.string().c_str(), ec.message().c_str());
        return 1;
    }
    app.scan = minidaw::scanDevices();
    minidaw::OpenResult r = app.audio.open(app.config.audio, app.scan);
    for (const auto& w : r.warnings) std::printf("warning: %s\n", w.c_str());
    if (!r.error.empty()) {
        std::printf("error: %s\n", r.error.c_str());
        return 1;
    }
    const auto& info = app.audio.info();
    if (info.inputChannels == 0) {
        std::printf("error: no input device is open, so there is nothing to record.\n");
        return 1;
    }

    minidaw::TakeSpec take;
    take.path = folder / takeFileName();
    take.channels = info.inputChannels >= 2 ? std::vector<unsigned int>{0, 1} : std::vector<unsigned int>{0};
    std::string error;
    minidaw::RecorderOptions options;
    options.syncToDisk = app.config.recording.syncToDisk;
    if (!app.audio.startRecording({take}, error, options)) {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }
    std::printf("Recording %s: %s, %u Hz, %zu ch, %d s\n", take.path.string().c_str(), info.inputName.c_str(),
                info.sampleRate, take.channels.size(), seconds);
    for (int t = 0; t < seconds * 10; ++t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!app.audio.recorder().diskError().empty()) break;
    }
    const minidaw::RecordingResult res = app.audio.stopRecording();
    const std::uint64_t xruns = app.audio.xruns();
    const double rate = info.sampleRate;  // info is reset by close()
    app.audio.close();

    for (const auto& t : res.takes) {
        std::printf("Wrote %s: %llu frames (%.2f s), lost frames %llu, clipped samples %llu\n", t.path.string().c_str(),
                    static_cast<unsigned long long>(t.frames), double(t.frames) / rate,
                    static_cast<unsigned long long>(t.droppedFrames),
                    static_cast<unsigned long long>(t.clippedSamples));
    }
    std::printf("dropouts (xruns): %llu\n", static_cast<unsigned long long>(xruns));
    if (!res.error.empty()) std::printf("error: %s\n", res.error.c_str());
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
        for (unsigned int ch = 0; ch < info.inputChannels; ++ch) {
            if (ch % 4 != 0) ImGui::SameLine(static_cast<float>(ch % 4) * 12 * ImGui::GetFontSize());
            ImGui::Text("In %u: %s", ch + 1, minidaw::formatDbfs(app.levelHold[ch]).c_str());
        }
    }
    for (const auto& w : app.audioWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    for (const auto& m : app.audio.takeDriverMessages()) app.audioWarnings.push_back(m);
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
            drawDevicesPanel(app.scan);
        }
        ImGui::EndChild();
        drawStatusLine(app);
        ImGui::End();

        drawAudioSettingsWindow(app);
        drawCustomContrastWindow(app);
        drawContrastReportWindow(app);

        ImGui::Render();
        const auto& bg = app.palette[minidaw::Role::Background];
        const float clear[4] = {static_cast<float>(bg.r), static_cast<float>(bg.g), static_cast<float>(bg.b), 1.0f};
        minidaw::app::rendererRender(window, clear);
    }

    app.audio.close();
    minidaw::app::rendererShutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
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
            audioTestSeconds = std::clamp(std::atoi(argv[++i]), 1, 600);
        } else if (std::strcmp(arg, "--record-test") == 0 && i + 2 < argc) {
            action = Action::RecordTest;
            audioTestSeconds = std::clamp(std::atoi(argv[++i]), 1, 3600);
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
