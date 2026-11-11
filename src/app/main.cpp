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

#include "folder_chooser.hpp"
#include "icon_data.hpp"
#include "imgui_theme.hpp"
#include "plugin_window.hpp"
#include "renderer.hpp"
#include "state_cues.hpp"

#include "minidaw/audio_stream.hpp"
#include "minidaw/config.hpp"
#include "minidaw/devices.hpp"
#include "minidaw/folder_browse.hpp"
#include "minidaw/instance_lock.hpp"
#include "minidaw/instrument.hpp"
#include "minidaw/plugin_scan.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/log.hpp"
#include "minidaw/meter.hpp"
#include "minidaw/midi_input.hpp"
#include "minidaw/midi_player.hpp"
#include "minidaw/monitor.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/project.hpp"
#include "minidaw/recording_session.hpp"
#include "minidaw/render.hpp"
#include "minidaw/run_loop.hpp"
#include "minidaw/script.hpp"
#include "minidaw/terms.hpp"
#include "minidaw/timeline.hpp"
#include "minidaw/track.hpp"
#include "minidaw/theme.hpp"
#include "minidaw/version.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // the tab bar's order after a drag (P18): GetCurrentTabBar, TabBarGetTabName
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
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
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
#include <fstream>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using minidaw::ContrastPreset;
using minidaw::ContrastSetting;

// ---------------------------------------------------------------------------
// A script being run (P25a, SPEC-RUN-02): `mini-daw --script <file>`.
struct ScriptRun {
    std::filesystem::path file;
    std::filesystem::path reportPath;
    std::ofstream report;  // written as it goes, so it is there up to a crash
    std::vector<minidaw::ScriptLine> lines;
    std::size_t next = 0;           // the next line to run
    bool stopOnFailure = false;
    bool stay = false;              // keep mini-daw open at the end
    bool done = false;
    int commands = 0;
    int failures = 0;
    // The line that is running over several frames: slow work, a scan, wait or wait-for.
    enum class Pending { None, Busy, Scan, Wait, WaitFor } pending = Pending::None;
    const minidaw::ScriptLine* current = nullptr;
    std::chrono::steady_clock::time_point started{};   // of the current line
    std::chrono::steady_clock::time_point deadline{};  // wait / wait-for
    bool busyResult = false;        // set by the slow work (open)
    int lastAdded = 0;              // the track the last add-track made ("last" as a track number)
    std::uint64_t logSeen = 0;      // log lines already copied into the report
};

// Application state
// ---------------------------------------------------------------------------
struct AppState {
    // Slow work started by a click (PL1, SPEC-UI-16): its message is drawn first, the
    // work runs at the start of the next frame (the window cannot draw meanwhile).
    struct BusyJob {
        std::string message;
        std::function<void()> work;
    };
    std::optional<BusyJob> busy;
    std::unique_ptr<ScriptRun> script;  // --script (P25a)
    int scriptExitCode = 0;
    // One window per config folder (P20, SPEC-RUN-01): held while the GUI runs.
    minidaw::InstanceLock instanceLock;
    std::string instanceLockNote;  // why there is no lock (logged once the log is open)
    float trackTableExtra = 0.0f;  // track table height the hand count missed (crosscheck #15, W8)

    // Presets of a track's instrument (PL2, SPEC-VST-07a): read when its menu opens or
    // < / > is used, for one track and instrument at a time.
    struct PresetMenu {
        int track = 0;
        std::uint64_t serial = 0;  // the instrument it was read for
        double readAt = 0.0;       // ImGui time of the reading (< / > read again after 3 s, X9)
        minidaw::Vst3Instrument::Programs programs;
        minidaw::PresetListing listing;
        std::vector<minidaw::PresetChoice> choices;  // programs, then files
        minidaw::PresetFolders folders;
    } presetMenu;
    std::map<int, std::string> presetIds;  // per track: the entry chosen last (this session; X2)
    // The Save preset window: for this track and instrument (0 = closed).
    int savePresetTrack = 0;
    std::uint64_t savePresetSerial = 0;
    minidaw::PresetFolders savePresetFolders;
    std::string savePresetName;
    std::string savePresetFor;  // the name the file below was made for (X10: not every frame)
    std::filesystem::path savePresetFile;
    bool savePresetExists = false;
    std::string savePresetError;

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
    double disabledOpacity = 1.0;                // how far disabled controls fade (PL1)

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
    std::vector<std::string> autoParts;    // auto record (P16): the last parts, newest first
    // Timeline tab (P17, SPEC-PLAY-07).
    std::vector<minidaw::RecordingInfo> recordings;  // in the takes folder, oldest first
    bool recordingsDirty = true;                      // read them again before drawing
    std::filesystem::path selectedRecording;
    // P21: a recording's name / note window, and the Delete question (empty = closed).
    std::filesystem::path recordingEdit;
    std::string recordingEditName, recordingEditNote, recordingEditError;
    std::vector<minidaw::RecordingMark> recordingEditMarks;    // shown, with Remove (P22)
    std::vector<minidaw::RecordingMark> recordingEditRemoved;  // removed on Save
    bool recordingEditUnreadable = false;  // its recording.json could not be read: Save asks first (Y4)
    bool recordingEditFocusNote = false;
    bool recordingEditFocus = false;
    std::filesystem::path recordingDelete;
    std::string recordingDeleteError;
    // P22: a note typed while recording (stored as a mark), and the project's note.
    std::string markText;
    std::string markResult;  // "Note at 0:12 saved" / an error
    bool markFocus = false;
    double markSeconds = -1.0;           // the moment the note was begun (its first key); -1: none
    std::filesystem::path markFolder;    // the recording the note line belongs to
    std::string projectNote;
    bool showProjectNote = false;
    std::string projectNoteEdit;
    std::filesystem::path livePart;                   // the auto record part being recorded
    std::size_t recordingsSeen = 0;                   // to follow new ones (scroll to the end)
    std::uint64_t recordingsVersion = 0;              // +1 per rescan: the cached layout below
    minidaw::TimelineLayout timelineLayout;           // T4: laid out again only when something changed
    std::uint64_t layoutVersion = ~0ull;
    float layoutPps = 0.0f, layoutMinWidth = 0.0f, layoutGapWidth = 0.0f;
    float timelineZoom = 4.0f;                        // pixels per second, at 16 px text
    std::filesystem::path previewFolder;              // a recording played on its own (not the tracks)
    std::uint64_t previewLength = 0;
    std::uint64_t songPosition = 0;                   // where the song was when a preview started (T5)
    std::string timelineNote;
    bool timelineShown = false;
    // Tabs (P18, SPEC-UI-12): order and last tab in config.tabs.
    bool tabsStarted = false;
    std::string selectTab;             // shown from the next frame (start, "Audio setup" links, Settings > Tabs)
    int tabBarGeneration = 0;          // +1 when the order is set from outside the tab bar (a new tab bar takes it)
    bool showTabsWindow = false;
    std::string tabsFocus;      // Tabs window: the tab moved to the top / bottom, its other button gets the focus (U4)
    bool tabsFocusUp = false;
    // Terms tab (P23, SPEC-UI-17)
    std::size_t runLoopDropped = 0;  // editorRunLoop().droppedCount() already logged (Z1)
    std::string termsFind;
    std::size_t termsJump = SIZE_MAX;   // a "See also" clicked: scroll to this term in the next frame
    std::size_t termsMarked = SIZE_MAX; // the term jumped to last (shown with "> ")
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

    // Instruments (P10). The rack (inside `audio`) is cleared before these are
    // unloaded (unloadAllInstruments at exit).
    minidaw::PluginCache pluginCache;
    minidaw::PluginScanner scanner;
    bool scanning = false;
    bool scanRetryBad = false;
    bool showInstruments = false;
    std::string instrumentFolderInput;
    std::string instrumentFolderError;   // Add folder refused (P19)
    std::string instrumentFolderNote;    // added, with a remark (no .vst3 in it yet)
    std::map<int, std::unique_ptr<minidaw::Vst3Instrument>> instruments;  // by track number
    std::string rackSignature;               // what the rack was configured for
    std::vector<std::pair<std::string, minidaw::SpscRing<minidaw::RawMidi>*>> rackTaps;
    std::map<int, std::pair<std::filesystem::path, std::shared_ptr<const std::vector<minidaw::MidiEvent>>>> takeEvents;
    unsigned int takeEventsRate = 0;           // the rate takeEvents' frames were made for (Q6)
    double instrumentStateAt = 0.0;
    std::vector<std::string> instrumentNotes;
    // Plugin editor windows (P10d), by track number; closed before their
    // instruments are unloaded.
    minidaw::app::PluginWindows pluginWindows;
    // Browse... buttons (P19, SPEC-UI-14): one folder chooser; what it fills in.
    enum class BrowseFor { None, InstrumentFolder, ProjectFolder, RecordingFolder, TakeFolder };
    minidaw::app::FolderChooser folderChooser;
    BrowseFor browseFor = BrowseFor::None;
    GLFWwindow* mainWindow = nullptr;
    // Rendering a MIDI take through its instrument (P11c): one at a time.
    minidaw::OfflineRenderer renderer;
    int renderTrack = 0;                     // the MIDI track being rendered; 0 = none
    std::filesystem::path renderOut;
    std::filesystem::path renderTake;        // the MIDI take it renders (the result is kept only for it, R8)

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
    app.disabledOpacity = result.disabledOpacity;
    minidaw::app::applyPalette(app.palette, app.disabledOpacity);
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

// Space (Play / Stop) and Home (to the start) are the transport's keys, also
// with keyboard navigation on (U1): ImGui's navigation uses them only when no one
// owns them (Space would press the control in focus, Home scroll to the top), so
// they are owned every frame (taking effect at the next NewFrame). Text fields
// get typed characters separately, so a space can still be typed in a name.
void claimTransportKeys()
{
    static const ImGuiID owner = ImHashStr("minidaw-transport-keys");
    ImGui::SetKeyOwner(ImGuiKey_Space, owner);
    ImGui::SetKeyOwner(ImGuiKey_Home, owner);
}

// Text size (SPEC-UI-08). Applied through ImGui's dynamic font size.
// During a frame ImGui sets style.FontSizeBase back to the size in use, so a
// change from a key or the menu only took effect at the next start; ImGui's
// own style editor sets _NextFrameFontSizeBase for this (applied at the next
// NewFrame). Before the first frame setting FontSizeBase is enough.
void setFontSize(AppState& app, float size, bool save)
{
    app.config.fontSize = std::clamp(size, minidaw::kMinFontSize, minidaw::kMaxFontSize);
    ImGui::GetStyle().FontSizeBase = app.config.fontSize;
    ImGui::GetStyle()._NextFrameFontSizeBase = app.config.fontSize;
    if (save) saveConfig(app);
}

// Spaces and tabs around a text removed.
std::string trimSpaces(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t") - a + 1);
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
    if (!app.previewFolder.empty()) {  // a recording played from the timeline: back to where the song was (T5)
        app.previewFolder.clear();
        app.audio.player().locate(app.songPosition);
    }
}

void reloadInstruments(AppState& app);  // P10, further down

// Slow work on the GUI thread (PL1, SPEC-UI-16): "<message> ..." is on screen before
// the window stops drawing, and the log says how long it took. One at a time.
void startBusy(AppState& app, std::string message, std::function<void()> work)
{
    if (app.busy) {
        // The click is not lost without a sign (crosscheck #15, W4).
        app.log.warning("Not started while \"" + app.busy->message + "\" is still to do: " + message);
        return;
    }
    app.busy = AppState::BusyJob{std::move(message), std::move(work)};
}

// At the start of a frame: the message was drawn in the frame before, so do the work.
void runBusy(AppState& app)
{
    if (!app.busy) return;
    AppState::BusyJob job = std::move(*app.busy);
    app.busy.reset();
    const auto start = std::chrono::steady_clock::now();
    job.work();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    char buf[64];
    std::snprintf(buf, sizeof buf, ": done in %.2f s", seconds);
    app.log.info(job.message + buf);
}

// The message, on top of everything (a new ImGui window is hidden in its first frame).
void drawBusy(const AppState& app)
{
    if (!app.busy) return;
    const std::string text = app.busy->message + " ...";
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = ImGui::GetFontSize();
    // Wrapped to the window, so a long project path stays inside it (W5).
    const float wrap = std::max(10.0f * pad, vp->WorkSize.x - 4.0f * pad);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
    const ImVec2 c(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f);
    const ImVec2 a(c.x - size.x * 0.5f - pad, c.y - size.y * 0.5f - pad);
    const ImVec2 b(c.x + size.x * 0.5f + pad, c.y + size.y * 0.5f + pad);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_WindowBg));
    dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_CheckMark), 0.0f, 0, minidaw::app::cueThickness());  // Accent
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + pad, a.y + pad), ImGui::GetColorU32(ImGuiCol_Text),
                text.c_str(), nullptr, wrap);
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
        reloadInstruments(app);  // instruments run at the stream rate
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

// One line of the contrast report: every colour pair, then the disabled ones as
// drawn (faded to disabledOpacity, SPEC-UI-02a).
struct ContrastRow {
    std::string foreground;
    std::string background;
    minidaw::Rgb fg;
    minidaw::Rgb bg;
    double ratio = 0.0;
};

std::vector<ContrastRow> contrastRows(const AppState& app)
{
    std::vector<ContrastRow> rows;
    for (const auto& pair : minidaw::colorPairs()) {
        const auto& fg = app.palette[pair.foreground];
        const auto& bg = app.palette[pair.background];
        rows.push_back({std::string(minidaw::roleName(pair.foreground)), std::string(minidaw::roleName(pair.background)),
                        fg, bg, minidaw::contrastRatio(fg, bg)});
    }
    char suffix[48];
    std::snprintf(suffix, sizeof suffix, ", faded to %.0f %%", app.disabledOpacity * 100.0);
    for (const auto& pair : minidaw::disabledPairs()) {
        const auto& bg = app.palette[pair.background];
        const auto fg = minidaw::mix(bg, app.palette[pair.foreground], app.disabledOpacity);
        rows.push_back({std::string(minidaw::roleName(pair.foreground)) + suffix,
                        std::string(minidaw::roleName(pair.background)), fg, bg, minidaw::contrastRatio(fg, bg)});
    }
    return rows;
}

int printContrastReport(const AppState& app)
{
    std::printf("Contrast setting: %s\n", describeContrast(app.config.contrast).c_str());
    std::printf("%-32s %-22s %-8s %-8s %8s  %s\n", "Foreground", "Background", "FG", "BG", "Ratio", "Result");
    bool allPass = true;
    for (const ContrastRow& row : contrastRows(app)) {
        const bool pass = minidaw::meetsContrast(row.ratio, app.config.contrast.minimum());
        allPass = allPass && pass;
        std::printf("%-32s %-22s %-8s %-8s %7.2f:1  %s\n", row.foreground.c_str(), row.background.c_str(),
                    minidaw::toHex(row.fg).c_str(), minidaw::toHex(row.bg).c_str(), row.ratio, pass ? "pass" : "FAIL");
    }
    return app.unresolved.empty() && allPass ? 0 : 1;
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
                const bool there = minidaw::isProjectFolder(minidaw::pathFromUtf8(r));
                const std::string label = there ? r : r + "  (not found)";  // N5
                if (ImGui::MenuItem(label.c_str(), nullptr, false, there)) {
                    const std::filesystem::path folder = minidaw::pathFromUtf8(r);
                    startBusy(app, "Opening project " + r, [&app, folder] { openProject(app, folder); });
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save project as...", nullptr, false, !rec)) {
            showProjectDialog(app, AppState::ProjectDialog::SaveAs);
        }
        if (ImGui::MenuItem("Close project", nullptr, false, !rec && !app.projectFolder.empty())) closeProject(app);
        if (ImGui::MenuItem("Project note...", nullptr, false, !app.projectFolder.empty())) {  // P22
            if (!app.showProjectNote) app.projectNoteEdit = app.projectNote;  // text being typed stays (Y2)
            app.showProjectNote = true;
        }
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
        if (ImGui::MenuItem("Instruments (VST\xc2\xae" "3)...")) app.showInstruments = true;
        if (ImGui::MenuItem("Tabs...")) app.showTabsWindow = true;
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

// --- Terms tab (P23, SPEC-UI-17) ------------------------------------------
// mini-daw's words explained (minidaw/terms.hpp): all by topic, or those found.
// "See also" jumps to that term in the whole list.
void drawTerm(AppState& app, std::size_t index, bool showGroup)
{
    const minidaw::Term& t = minidaw::terms()[index];
    ImGui::PushID(static_cast<int>(index));
    if (app.termsJump == index) {
        ImGui::SetScrollHereY(0.0f);
        app.termsJump = SIZE_MAX;
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
    const std::string title = (app.termsMarked == index ? "> " : "") + t.name;
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    if (showGroup) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", t.group.c_str());
    }
    ImGui::Indent();
    ImGui::TextWrapped("%s", t.text.c_str());
    if (!t.where.empty()) ImGui::TextWrapped("Where: %s", t.where.c_str());
    if (!t.seeAlso.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("See also:");
        for (const std::string& other : t.seeAlso) {
            const std::size_t to = minidaw::termIndex(other);
            if (to >= minidaw::terms().size()) continue;
            // Wraps to the next line when the button would not fit.
            const float w = ImGui::CalcTextSize(other.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            const float right = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + w;
            if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
            if (ImGui::Button(other.c_str())) {
                app.termsFind.clear();
                app.termsJump = to;
                app.termsMarked = to;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to \"%s\"", other.c_str());
        }
    }
    ImGui::Unindent();
    ImGui::Spacing();
    ImGui::PopID();
}

void drawTermsTab(AppState& app)
{
    ImGui::TextWrapped("The words mini-daw uses, and what they mean. Type a word to find it.");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Find:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(24.0f * ImGui::GetFontSize(), ImGui::GetContentRegionAvail().x * 0.6f));
    if (ImGui::InputTextWithHint("##terms-find", "a word, e.g. take", &app.termsFind)) {
        app.termsMarked = SIZE_MAX;  // a new search: the "> " of the last jump goes (Z7)
    }
    ImGui::SameLine();
    minidaw::app::beginDisabled(app.termsFind.empty());
    if (ImGui::Button("Show all")) {
        app.termsFind.clear();
        app.termsMarked = SIZE_MAX;
    }
    minidaw::app::endDisabled();
    // The topic view only for an empty Find; any word gives the result list,
    // even one every term has (Z8).
    const std::string query = minidaw::trimmedTermQuery(app.termsFind);
    const std::vector<std::size_t> found = minidaw::findTerms(query);
    const bool all = query.empty();
    ImGui::SameLine();
    if (all || found.size() == minidaw::terms().size()) {
        ImGui::Text("%zu terms", found.size());
    } else {
        ImGui::Text("%zu of %zu terms", found.size(), minidaw::terms().size());
    }
    if (ImGui::BeginChild("terms-list", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (found.empty()) {
            ImGui::TextWrapped("No term has \"%s\". Try a shorter word, or Show all.", query.c_str());
        } else if (all) {
            std::string group;
            for (std::size_t i : found) {
                const minidaw::Term& t = minidaw::terms()[i];
                if (t.group != group) {
                    group = t.group;
                    ImGui::SeparatorText(group.c_str());
                }
                drawTerm(app, i, false);
            }
        } else {
            for (std::size_t i : found) drawTerm(app, i, true);
        }
    }
    ImGui::EndChild();
}

// --- Tabs (P18, SPEC-UI-12) ------------------------------------------------
const char* tabTitle(const std::string& id)
{
    if (id == "timeline") return "Timeline";
    if (id == "audio") return "Audio setup";
    if (id == "terms") return "Terms";
    return "Recording";
}

// The label ImGui gets: the title, and the id after "###" (the tab's identity).
std::string tabLabel(const std::string& id) { return std::string(tabTitle(id)) + "###tab-" + id; }

void setTabOrder(AppState& app, std::vector<std::string> order)
{
    order = minidaw::normalizeTabOrder(order);
    if (order == app.config.tabs.order) return;
    app.config.tabs.order = std::move(order);
    saveConfig(app);
}

void drawTabsWindow(AppState& app)
{
    if (!app.showTabsWindow) return;
    ImGui::SetNextWindowSize(ImVec2(28 * ImGui::GetFontSize(), 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Tabs", &app.showTabsWindow, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextWrapped("The order of the tabs, left to right. You can also drag a tab to another place. "
                           "At start the tab used last is shown.");
        // Rows are keyed by tab, so the keyboard focus moves with the tab that was
        // moved. When it reaches the top or bottom (that button is then disabled)
        // the focus goes to its other button (crosscheck #13, U4).
        std::vector<std::string> order = app.config.tabs.order;
        bool changed = false;
        const bool keyboard = ImGui::GetIO().NavVisible;
        const std::string focusTab = app.tabsFocus;  // set by a move in the frame before, used once
        const bool focusUp = app.tabsFocusUp;
        app.tabsFocus.clear();
        // The move is made after all rows are drawn: drawn at once, the moved tab's
        // row would come again in this frame and its button act twice.
        std::size_t moveFrom = order.size();
        std::size_t moveTo = order.size();
        for (std::size_t i = 0; i < order.size(); ++i) {
            const std::string id = order[i];
            ImGui::PushID(id.c_str());
            minidaw::app::beginDisabled(i == 0);
            if (keyboard && focusTab == id && focusUp) ImGui::SetKeyboardFocusHere();
            if (ImGui::Button("Up") && i > 0) {
                moveFrom = i;
                moveTo = i - 1;
            }
            minidaw::app::endDisabled();
            ImGui::SameLine();
            minidaw::app::beginDisabled(i + 1 == order.size());
            if (keyboard && focusTab == id && !focusUp) ImGui::SetKeyboardFocusHere();
            if (ImGui::Button("Down") && i + 1 < order.size()) {
                moveFrom = i;
                moveTo = i + 1;
            }
            minidaw::app::endDisabled();
            ImGui::SameLine();
            ImGui::Text("%zu. %s", i + 1, tabTitle(id));
            ImGui::PopID();
        }
        if (moveFrom < order.size()) {
            const std::string id = order[moveFrom];
            std::swap(order[moveFrom], order[moveTo]);
            if (moveTo == 0 || moveTo + 1 == order.size()) {  // its button there is disabled: focus the other one
                app.tabsFocus = id;
                app.tabsFocusUp = moveTo != 0;
            }
            changed = true;
        }
        if (ImGui::Button("Default order")) {
            order = minidaw::defaultTabOrder();
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close")) app.showTabsWindow = false;
        if (changed && order != app.config.tabs.order) {
            setTabOrder(app, order);
            ++app.tabBarGeneration;  // a new tab bar shows the tabs in this order
            app.selectTab = app.config.tabs.last;
        }
    }
    ImGui::End();
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
            for (const ContrastRow& row : contrastRows(app)) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(row.foreground.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(row.background.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(minidaw::toHex(row.fg).c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(minidaw::toHex(row.bg).c_str());
                ImGui::TableNextColumn(); ImGui::Text("%.2f:1", row.ratio);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(minidaw::meetsContrast(row.ratio, app.config.contrast.minimum()) ? "pass" : "FAIL");
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
        if (minidaw::app::choice(minidaw::kDefaultDevice, value == minidaw::kDefaultDevice)) value = minidaw::kDefaultDevice;
        if (minidaw::app::choice(minidaw::kNoDevice, value == minidaw::kNoDevice)) value = minidaw::kNoDevice;
        for (const auto& d : scan.audioDevices) {
            const unsigned int ch = input ? d.inputChannels : d.outputChannels;
            if (ch == 0) continue;
            ImGui::PushID(static_cast<int>(d.id));
            const std::string text = d.name + "  (" + std::to_string(ch) + " ch)";
            if (minidaw::app::choice(text.c_str(), value == d.name)) value = d.name;
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
            if (minidaw::app::choice(text.c_str(), value == c)) value = c;
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
        if (ImGui::Button("Apply")) {
            startBusy(app, "Opening the audio devices", [&app] { applyAudioSettings(app, app.pendingAudio, true); });
        }
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
    // Fixed space for the text and the CLIP button, so that nothing to the
    // right moves when the value gets longer or CLIP appears. The widest
    // value is "-1xx.x dBFS" (formatDbfs); "8" is used as the widest digit.
    ImGui::SameLine();
    const std::string value = minidaw::formatDbfs(text);
    const float valueWidth = std::max(ImGui::CalcTextSize("-888.8 dBFS").x, ImGui::CalcTextSize("-inf dBFS").x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value.c_str());
    const float pad = valueWidth - ImGui::CalcTextSize(value.c_str()).x;
    if (pad > 0.0f) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(pad, 1.0f));
    }
    ImGui::SameLine();
    const ImVec2 clipSize(ImGui::CalcTextSize("CLIP").x + 2.0f * ImGui::GetStyle().FramePadding.x,
                          ImGui::GetTextLineHeight());
    if (clipped) {
        if (ImGui::SmallButton("CLIP")) {
            for (auto* m : meters) m->clearClip();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The level reached 0 dBFS. Click to clear.");
    } else {
        ImGui::Dummy(clipSize);  // keeps the space of the CLIP button
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
}

// Every frame, whatever tab is shown: the driver's messages to the log and the notes.
void pollAudioMessages(AppState& app)
{
    for (const auto& m : app.audio.takeDriverMessages()) {
        app.log.warning("Audio driver: " + m);  // every one in the log
        minidaw::addNote(app.audioWarnings, m);  // repeats counted, the list kept short (U8)
    }
}

// The Recording tab's short audio line (P18): the full "Audio stream" is on the Audio setup tab.
void drawAudioStatusLine(AppState& app)
{
    const auto& info = app.audio.info();
    // The button goes in front of the text: after a long wrapped text it would be
    // outside the window (seen under Wine, P19). One label per place: unique IDs (U3).
    const auto goToSetup = [&app](const char* label) {
        if (ImGui::SmallButton(label)) app.selectTab = "audio";
        ImGui::SameLine();
    };
    if (!info.running) {
        goToSetup("Audio setup##stopped");
        ImGui::TextWrapped("Audio stream stopped. %s", app.audioError.c_str());
        return;
    }
    if (app.streamStalled) {
        goToSetup("Audio setup##stalled");
        ImGui::TextWrapped("ERROR: the audio stream has stopped (no audio callback for more than 1 second). "
                           "Try Settings > Audio devices > Apply.");
    }
    ImGui::Text("Audio: %s, %u Hz, %u frames   Dropouts: %llu", info.api.c_str(), info.sampleRate, info.bufferFrames,
                static_cast<unsigned long long>(app.audio.xruns()));
    if (info.outputChannels > 0) {
        ImGui::SameLine();
        ImGui::TextUnformatted("  Output:");
        ImGui::SameLine();
        std::vector<minidaw::LevelMeter*> out;
        for (unsigned int ch = 0; ch < info.outputChannels && ch < minidaw::kMaxOutputChannels; ++ch) {
            out.push_back(&app.outputMeters[ch]);
        }
        drawMeter(app, out, 10 * ImGui::GetFontSize());
    }
    if (!app.audioWarnings.empty()) {
        goToSetup("Audio setup##notes");
        ImGui::Text("%zu note(s) about the audio stream.", app.audioWarnings.size());
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

// --- Instruments (P10) ------------------------------------------------------
std::filesystem::path pluginCacheFile(const AppState& app)
{
    return app.configPath.parent_path() / "plugins.json";
}

std::filesystem::path pluginScanMarker(const AppState& app)
{
    return app.configPath.parent_path() / "plugin-scan.inprogress";
}

std::vector<std::filesystem::path> instrumentFolders(const AppState& app)
{
    std::vector<std::filesystem::path> f = minidaw::standardVst3Folders();
    for (const auto& extra : app.config.instruments.folders) f.push_back(minidaw::pathFromUtf8(extra));
    return f;
}

std::string instrumentLabel(const minidaw::PluginClass& c)
{
    return c.vendor.empty() ? c.name : c.name + " (" + c.vendor + ")";
}

void startScan(AppState& app)
{
    app.scanner.start(instrumentFolders(app), app.pluginCache, pluginScanMarker(app), app.scanRetryBad);
    app.scanning = true;
    app.log.info("Scanning for VST3 instruments: " + std::to_string(app.scanner.total()) + " module(s)");
}

// One module per frame, so the window stays responsive (SPEC-VST-01a).
void stepScan(AppState& app)
{
    if (!app.scanning) return;
    const std::string current = app.scanner.current();
    if (!current.empty()) app.log.info("Scanning " + current);
    if (app.scanner.step()) return;
    app.scanning = false;
    app.pluginCache = app.scanner.result();
    std::string err;
    if (!minidaw::savePluginCache(pluginCacheFile(app), app.pluginCache, err)) app.log.error(err);
    std::size_t bad = 0;
    for (const auto& m : app.pluginCache.modules) {
        if (m.status == minidaw::ModuleStatus::Failed || m.status == minidaw::ModuleStatus::Crashed) {
            ++bad;
            app.log.warning(std::string("Instrument module ") + minidaw::moduleStatusName(m.status) + ": " + m.path +
                            (m.error.empty() ? "" : " - " + m.error));
        }
    }
    app.log.info("Scan done: " + std::to_string(app.pluginCache.instruments().size()) + " instrument(s), " +
                 std::to_string(bad) + " module(s) with problems");
}

// At start: a scan that did not finish means a module crashed mini-daw.
void checkScanMarker(AppState& app)
{
    // Marks the module (adding it if this was its first scan) and removes the marker.
    const std::string crashed = minidaw::recordScanCrash(pluginScanMarker(app), app.pluginCache);
    if (crashed.empty()) return;
    std::string err;
    minidaw::savePluginCache(pluginCacheFile(app), app.pluginCache, err);
    const std::string note = "mini-daw stopped while scanning " + crashed + " last time; that module is skipped "
                             "(Settings > Instruments can try it again).";
    app.instrumentNotes.push_back(note);
    app.log.warning(note);
}

unsigned int instrumentRate(const AppState& app)
{
    return app.audio.info().sampleRate ? app.audio.info().sampleRate : app.config.audio.sampleRate;
}

// The plugins' current state into the tracks (for the project file).
void refreshInstrumentStates(AppState& app)
{
    for (auto& t : app.tracks) {
        const auto it = app.instruments.find(t.number);
        if (it == app.instruments.end() || !it->second->loaded()) continue;
        std::string comp, ctrl, err;
        if (it->second->getState(comp, ctrl, err)) {
            t.instrumentState = std::move(comp);
            t.instrumentControllerState = std::move(ctrl);
        }
    }
}

void syncRack(AppState& app, bool force = false);

void unloadInstrument(AppState& app, int track)
{
    const auto it = app.instruments.find(track);
    if (it == app.instruments.end()) return;
    app.pluginWindows.close(track);
    std::unique_ptr<minidaw::Vst3Instrument> inst = std::move(it->second);
    app.instruments.erase(it);
    syncRack(app, true);  // the audio thread lets go of it first
    inst->unload();
}

void cancelRender(AppState& app);  // further down

void unloadAllInstruments(AppState& app)
{
    cancelRender(app);  // its tracks are about to change (project switch, quit)
    app.pluginWindows.closeAll();
    app.audio.instruments().clear();
    for (const auto& [port, ring] : app.rackTaps) app.midi.removeTap(port, ring);
    app.rackTaps.clear();
    app.rackSignature.clear();
    for (auto& [n, inst] : app.instruments) inst->unload();
    app.instruments.clear();
}

// Loads the instrument a track names (with its saved state). Reports a
// missing or failing plugin as a note; the track keeps its settings.
bool loadTrackInstrument(AppState& app, minidaw::Track& t)
{
    unloadInstrument(app, t.number);
    if (t.instrumentClass.empty()) return false;
    std::filesystem::path module = minidaw::pathFromUtf8(t.instrumentModule);
    if (const minidaw::PluginClass* c = app.pluginCache.findClass(t.instrumentClass)) {
        module = minidaw::pathFromUtf8(c->modulePath);  // it may have moved since the project was saved
        if (t.instrumentName.empty()) t.instrumentName = c->name;
    }
    auto inst = std::make_unique<minidaw::Vst3Instrument>();
    std::string err;
    std::error_code ec;
    if (module.empty() || !std::filesystem::exists(module, ec) ||
        !inst->load(module, t.instrumentClass, instrumentRate(app), err)) {
        const std::string note = "Track " + std::to_string(t.number) + ": instrument \"" +
                                 (t.instrumentName.empty() ? t.instrumentClass : t.instrumentName) +
                                 "\" could not be loaded" + (err.empty() ? " (not found; scan in Settings > Instruments)" : ": " + err);
        app.instrumentNotes.push_back(note);
        app.log.warning(note);
        return false;
    }
    if (!t.instrumentState.empty() && !inst->setState(t.instrumentState, t.instrumentControllerState, err)) {
        app.log.warning("Track " + std::to_string(t.number) + ": " + err);
    }
    t.instrumentModule = minidaw::pathToUtf8(module);
    app.log.info("Track " + std::to_string(t.number) + ": instrument " + inst->name() + " loaded");
    app.instruments[t.number] = std::move(inst);
    syncRack(app, true);
    return true;
}

void chooseInstrument(AppState& app, minidaw::Track& t, const minidaw::PluginClass& c)
{
    t.instrumentClass = c.classId;
    t.instrumentModule = c.modulePath;
    t.instrumentName = c.name;
    t.instrumentState.clear();
    t.instrumentControllerState.clear();
    t.instrumentPreset.clear();
    t.midiOut.clear();  // the instrument plays instead
    loadTrackInstrument(app, t);
}

void restoreInstruments(AppState& app)
{
    unloadAllInstruments(app);
    for (auto& t : app.tracks) {
        if (t.kind == minidaw::TrackKind::Midi && !t.instrumentClass.empty()) loadTrackInstrument(app, t);
    }
}

// After the sample rate changed: load again at the new rate, with the state.
void openPluginWindow(AppState& app, const minidaw::Track& t);  // further down

void reloadInstruments(AppState& app)
{
    // Only a new sample rate needs new instruments (the block size does not:
    // they take up to 4096 frames per call and larger blocks in parts) (Q7).
    bool sameRate = true;
    for (const auto& [n, inst] : app.instruments) {
        if (inst->loaded() && inst->sampleRate() != static_cast<double>(instrumentRate(app))) sameRate = false;
    }
    if (app.instruments.empty() || sameRate) return;
    refreshInstrumentStates(app);
    const std::vector<int> editors = app.pluginWindows.openTracks();  // opened again afterwards
    restoreInstruments(app);
    for (const int n : editors) {
        for (const auto& t : app.tracks) {
            if (t.number == n) openPluginWindow(app, t);
        }
    }
}

minidaw::Track* findTrack(AppState& app, int number)
{
    for (auto& t : app.tracks) {
        if (t.number == number) return &t;
    }
    return nullptr;
}

// The audio track that holds the sound of MIDI track `midiNumber`'s
// instrument (P11, SPEC-VST-03a), made if missing: right below the MIDI
// track, silent at first ("Sound from" = instrument). Changes app.tracks
// (no references into it may be held across this call). Returns its number.
int ensureLinkedTrack(AppState& app, int midiNumber)
{
    const bool had = minidaw::linkedSoundTrack(app.tracks, midiNumber) != nullptr;
    const int n = minidaw::addLinkedSoundTrack(app.tracks, midiNumber);
    if (n != 0 && !had) {
        const minidaw::Track* s = findTrack(app, n);
        app.log.info("Track " + std::to_string(n) + " \"" + (s ? s->name : std::string()) +
                     "\" holds the sound of track " + std::to_string(midiNumber) + "'s instrument");
    }
    return n;
}

// Opens the plugin's own editor window for a track (SPEC-VST-05a).
void openPluginWindow(AppState& app, const minidaw::Track& t)
{
    const auto it = app.instruments.find(t.number);
    if (it == app.instruments.end()) return;
    const std::string title = it->second->name() + " - " + t.name + " - " + minidaw::kProductName;
    std::string err;
    if (app.pluginWindows.open(t.number, *it->second, title, app.mainWindow, err)) {
        app.log.info("Track " + std::to_string(t.number) + ": editor of " + it->second->name() + " opened");
    } else {
        const std::string note = "Track " + std::to_string(t.number) + ": the editor of " + it->second->name() +
                                 " could not be opened: " + err;
        app.instrumentNotes.push_back(note);
        app.log.warning(note);
    }
}

// --- Rendering a MIDI take through its instrument (P11c, SPEC-VST-04a) --------

// Starts rendering MIDI track `number`'s take with a second instance of its
// instrument (its current settings) into "NN-<linked name>-render.wav" next
// to the take. The linked track is made if needed.
void startRender(AppState& app, int number)
{
    if (app.renderTrack != 0 || app.session.recording()) return;
    // Everything is checked before the linked track is made (R8).
    minidaw::Track* t = findTrack(app, number);
    const auto it = app.instruments.find(number);
    if (t == nullptr || it == app.instruments.end() || t->take.empty()) return;
    refreshInstrumentStates(app);  // the settings as they are now
    std::string err;
    minidaw::SmfData d;
    if (!minidaw::readSmf(t->take, d, err)) {
        app.instrumentNotes.push_back("Track " + std::to_string(number) + ": the take could not be read: " + err);
        return;
    }
    const bool hadLinked = minidaw::linkedSoundTrack(app.tracks, number) != nullptr;
    const int linkedNumber = ensureLinkedTrack(app, number);  // may move tracks: look them up again
    t = findTrack(app, number);
    minidaw::Track* linked = findTrack(app, linkedNumber);
    if (t == nullptr || linked == nullptr) return;
    minidaw::RenderJob job;
    job.module = it->second->modulePath();
    job.classId = it->second->classId();
    job.state = t->instrumentState;
    job.controllerState = t->instrumentControllerState;
    job.sampleRate = instrumentRate(app);
    job.events = minidaw::smfToFrames(d, job.sampleRate);
    job.takeFrames = t->takeFrames;
    job.out = minidaw::newRenderPath(t->take.parent_path(), *linked);  // never over a take that plays (R1)
    app.renderOut = job.out;
    app.renderTake = t->take;
    if (!app.renderer.start(std::move(job), err)) {
        const std::string note = "Track " + std::to_string(number) + ": rendering could not start: " + err;
        app.instrumentNotes.push_back(note);
        app.log.warning(note);
        if (!hadLinked) {  // no empty linked track left behind
            for (std::size_t i = 0; i < app.tracks.size(); ++i) {
                if (app.tracks[i].number == linkedNumber) {
                    app.log.info("Track " + std::to_string(linkedNumber) + " \"" + app.tracks[i].name +
                                 "\" removed (rendering could not start)");  // BB2
                    minidaw::removeTrack(app.tracks, i);
                    break;
                }
            }
        }
        return;
    }
    app.renderTrack = number;
    app.log.info("Track " + std::to_string(number) + ": rendering its take into " +
                 minidaw::pathToUtf8(app.renderOut.filename()));
}

// Once per frame: a finished render goes on the linked track, at the MIDI take's position.
void pollRender(AppState& app)
{
    if (app.renderTrack == 0 || !app.renderer.done()) return;
    const minidaw::RenderResult r = app.renderer.finish();
    const int number = app.renderTrack;
    app.renderTrack = 0;
    if (r.cancelled) {
        app.log.info("Track " + std::to_string(number) + ": rendering cancelled");
        return;
    }
    if (!r.ok) {
        const std::string note = "Track " + std::to_string(number) + ": rendering failed: " + r.error;
        app.instrumentNotes.push_back(note);
        app.log.error(note);
        return;
    }
    minidaw::Track* t = findTrack(app, number);
    // Only for the same track with the same take (a track number can be reused, R8).
    if (t != nullptr && (t->kind != minidaw::TrackKind::Midi || t->take != app.renderTake)) t = nullptr;
    minidaw::Track* linked = t ? minidaw::linkedSoundTrack(app.tracks, number) : nullptr;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%.1f s, peak %.1f dBFS", static_cast<double>(r.frames) / instrumentRate(app),
                  r.peak > 0 ? 20.0 * std::log10(static_cast<double>(r.peak)) : -120.0);
    if (linked == nullptr) {  // the track went away meanwhile: the file stays
        app.log.info("Rendered " + minidaw::pathToUtf8(app.renderOut) + " (" + buf + ")");
        return;
    }
    linked->take = app.renderOut;
    linked->takeStart = t->takeStart;
    refreshTakeLength(app, *linked);
    app.log.info("Track " + std::to_string(linked->number) + " holds the rendered sound of track " +
                 std::to_string(number) + " (" + buf + "); \"Sound from\" on that track chooses what plays");
}

void cancelRender(AppState& app)
{
    if (app.renderTrack == 0) return;
    app.renderer.cancel();
    while (!app.renderer.done()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    pollRender(app);
}

// The rack follows the tracks: live MIDI for armed tracks, the take during
// playback (not for an armed track while recording). Rebuilt when anything
// it depends on changes.
void syncRack(AppState& app, bool force)
{
    // A take's events are in frames at the instruments' rate: made again after a rate change (Q6).
    if (app.takeEventsRate != instrumentRate(app)) {
        app.takeEvents.clear();
        app.takeEventsRate = instrumentRate(app);
        force = true;
    }
    // Auto record counts as recording for the armed tracks (P16).
    const bool armedRecord = app.playbackForRecording || app.session.autoMode();
    std::string sig = armedRecord ? "R" : "P";
    for (const auto& t : app.tracks) {
        const auto it = app.instruments.find(t.number);
        if (t.kind != minidaw::TrackKind::Midi || it == app.instruments.end()) continue;
        sig += "|" + std::to_string(t.number) + (t.armed ? "a" : "-") + t.midiPort + "#" +
               std::to_string(t.midiChannel) + "@" + minidaw::pathToUtf8(t.take) + ":" +
               std::to_string(t.takeStart) + (t.soundFromRecording ? "r" : "i") + "&" +
               std::to_string(reinterpret_cast<std::uintptr_t>(it->second.get()));
    }
    sig += "/" + std::to_string(instrumentRate(app));
    if (!force && sig == app.rackSignature) return;
    app.rackSignature = sig;

    for (const auto& [port, ring] : app.rackTaps) app.midi.removeTap(port, ring);
    app.rackTaps.clear();
    std::vector<minidaw::InstrumentRack::SlotSetup> slots;
    std::vector<std::string> livePorts;
    for (const auto& t : app.tracks) {
        const auto it = app.instruments.find(t.number);
        if (t.kind != minidaw::TrackKind::Midi || it == app.instruments.end() || !it->second->loaded()) continue;
        if (slots.size() >= minidaw::InstrumentRack::kMaxSlots) break;
        minidaw::InstrumentRack::SlotSetup s;
        s.instrument = it->second.get();
        s.track = t.number;
        s.liveChannel = t.midiChannel;
        s.live = t.armed && !t.midiPort.empty();
        s.gain = minidaw::playbackGain(t, app.tracks);
        // The take's MIDI goes to the instrument unless the track plays its
        // recorded sound instead ("Sound from", P11).
        if (!t.take.empty() && !(t.armed && armedRecord) && minidaw::instrumentPlaysTake(t)) {
            auto& cached = app.takeEvents[t.number];
            if (cached.first != t.take || !cached.second) {
                minidaw::SmfData d;
                std::string err;
                cached.first = t.take;
                cached.second = minidaw::readSmf(t.take, d, err)
                                    ? std::make_shared<const std::vector<minidaw::MidiEvent>>(
                                          minidaw::smfToFrames(d, instrumentRate(app)))
                                    : nullptr;
            }
            s.playback = cached.second;
            s.takeStart = t.takeStart;
        }
        slots.push_back(s);
        livePorts.push_back(s.live ? t.midiPort : std::string());
    }
    app.audio.instruments().configure(slots);
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (livePorts[i].empty()) continue;
        minidaw::SpscRing<minidaw::RawMidi>* ring = app.audio.instruments().liveRing(i);
        if (app.midi.addTap(livePorts[i], ring)) app.rackTaps.emplace_back(livePorts[i], ring);
    }
}

// Every frame: idle the plugins (controller updates), keep the rack in step,
// and keep the saved states fresh for the project (every 5 s).
void updateInstruments(AppState& app)
{
    pollRender(app);
    for (auto& [n, inst] : app.instruments) inst->idle();
    // restartComponent requests (Q5). Parameter value / title changes need
    // nothing here (mini-daw shows no plugin parameters); new busses or a
    // reload request: the plugin is loaded again with its state.
    std::vector<int> reload;
    for (auto& [n, inst] : app.instruments) {
        const minidaw::Vst3Instrument::RestartRequest r = inst->takeRestartRequest();
        if (!r.any) continue;
        char flags[16];
        std::snprintf(flags, sizeof flags, "0x%X", static_cast<unsigned>(r.flags));
        std::string text = "Track " + std::to_string(n) + ": " + inst->name() + " asked for a restart (flags " + flags + ")";
        if (r.latencyChanged) text += "; latency now " + std::to_string(inst->latencySamples()) + " samples";
        if (r.reload) text += "; loading it again with its settings";
        app.log.info(text);
        if (r.reload) reload.push_back(n);
    }
    if (!reload.empty()) {
        refreshInstrumentStates(app);
        for (const int n : reload) {
            const bool editor = app.pluginWindows.isOpen(n);
            for (auto& t : app.tracks) {
                if (t.number != n) continue;
                if (loadTrackInstrument(app, t) && editor) openPluginWindow(app, t);
            }
        }
    }
    syncRack(app);
    const double now = ImGui::GetTime();
    if (!app.instruments.empty() && now >= app.instrumentStateAt) {
        app.instrumentStateAt = now + 5.0;
        refreshInstrumentStates(app);
    }
}

// A Browse... button after a folder field (P19, SPEC-UI-14): opens the folder
// chooser in the folder typed (or the nearest one above it), else `fallback`.
void browseButton(AppState& app, AppState::BrowseFor what, const std::string& typed,
                  const std::filesystem::path& fallback, const char* title)
{
    ImGui::SameLine();
    ImGui::PushID(static_cast<int>(what));
    minidaw::app::beginDisabled(app.folderChooser.active());
    if (ImGui::Button("Browse...")) {
        app.browseFor = what;
        app.folderChooser.open(app.mainWindow, title, minidaw::browseStart(typed, fallback));
    }
    minidaw::app::endDisabled();
    ImGui::PopID();
}

// Add folder (P19): the folder must exist, be new and not a standard one.
void addInstrumentFolder(AppState& app)
{
    std::string note;
    const std::string error = minidaw::addInstrumentFolder(app.config.instruments.folders, app.instrumentFolderInput,
                                                           minidaw::standardVst3Folders(), note);
    app.instrumentFolderError = error;
    app.instrumentFolderNote.clear();
    if (!error.empty()) {
        app.log.warning("Instrument folder not added: " + error);  // also seen if its window is closed (V7)
        return;
    }
    const std::string text = app.config.instruments.folders.back();
    app.instrumentFolderInput.clear();
    app.instrumentFolderNote = "Added " + text + (note.empty() ? "." : ". " + note) + " Press Scan to look for instruments.";
    app.log.info("Instrument folder added: " + text);
    saveConfig(app);
}

void drawInstrumentsWindow(AppState& app)
{
    if (!app.showInstruments) return;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(56 * em, 32 * em), ImGuiCond_Appearing);
    if (ImGui::Begin("Instruments (VST\xc2\xae" "3)###instruments", &app.showInstruments)) {
        ImGui::TextWrapped("VST is a registered trademark of Steinberg Media Technologies GmbH.");  // SPEC-LIC-05
        ImGui::SeparatorText("Folders");
        ImGui::TextUnformatted("Always searched:");
        for (const auto& f : minidaw::standardVst3Folders()) ImGui::BulletText("%s", minidaw::pathToUtf8(f).c_str());
        if (!app.config.instruments.folders.empty()) ImGui::TextUnformatted("Added by you:");
        int removeIndex = -1;
        for (std::size_t i = 0; i < app.config.instruments.folders.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            // The button first: after a long path it would be outside the window (P19).
            if (ImGui::SmallButton("Remove")) removeIndex = static_cast<int>(i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Take this folder off the list (nothing is deleted)");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", app.config.instruments.folders[i].c_str());
            ImGui::PopID();
        }
        if (removeIndex >= 0) {
            const std::string gone = app.config.instruments.folders[static_cast<std::size_t>(removeIndex)];
            app.config.instruments.folders.erase(app.config.instruments.folders.begin() + removeIndex);
            app.instrumentFolderError.clear();
            app.instrumentFolderNote = "Removed " + gone + " from the list. Press Scan to update the instruments.";
            app.log.info("Instrument folder removed: " + gone);
            saveConfig(app);
        }
        const bool full = app.config.instruments.folders.size() >= 8;
        minidaw::app::beginDisabled(full);
        ImGui::SetNextItemWidth(36 * em);
        const bool enter = ImGui::InputText("##folder", &app.instrumentFolderInput, ImGuiInputTextFlags_EnterReturnsTrue);
        const auto standard = minidaw::standardVst3Folders();
        browseButton(app, AppState::BrowseFor::InstrumentFolder, app.instrumentFolderInput,
                     standard.empty() ? minidaw::homeFolder() : standard.front(), "Choose a folder with VST3 instruments");
        ImGui::SameLine();
        minidaw::app::beginDisabled(minidaw::cleanPathText(app.instrumentFolderInput).empty());
        if (ImGui::Button("Add folder") || (enter && !minidaw::cleanPathText(app.instrumentFolderInput).empty())) {
            addInstrumentFolder(app);
        }
        minidaw::app::endDisabled();
        minidaw::app::endDisabled();
        if (full) ImGui::TextUnformatted("8 folders added (the most): remove one to add another.");
        if (!app.instrumentFolderError.empty()) ImGui::TextWrapped("Not added: %s", app.instrumentFolderError.c_str());
        if (!app.instrumentFolderNote.empty()) ImGui::TextWrapped("%s", app.instrumentFolderNote.c_str());

        ImGui::SeparatorText("Scan");
        minidaw::app::beginDisabled(app.scanning);
        if (ImGui::Button("Scan")) startScan(app);
        ImGui::SameLine();
        ImGui::Checkbox("Also try modules that failed or crashed before", &app.scanRetryBad);
        minidaw::app::endDisabled();
        if (app.scanning) {
            ImGui::TextWrapped("Scanning %zu of %zu: %s", app.scanner.done() + 1, app.scanner.total(),
                               app.scanner.current().c_str());
        }
        for (const auto& n : app.instrumentNotes) ImGui::TextWrapped("Note: %s", n.c_str());

        const auto list = app.pluginCache.instruments();
        ImGui::SeparatorText(("Instruments found: " + std::to_string(list.size())).c_str());
        if (list.empty()) ImGui::TextWrapped("None yet. Press Scan (VST3 instruments in the folders above).");
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (!list.empty() && ImGui::BeginTable("instruments", 4, flags, ImVec2(0, 12 * ImGui::GetTextLineHeightWithSpacing()))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Vendor");
            ImGui::TableSetupColumn("Version");
            ImGui::TableSetupColumn("Module", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& c : list) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.name.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.vendor.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.version.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(c.modulePath.c_str());
            }
            ImGui::EndTable();
        }
        bool header = false;
        for (const auto& m : app.pluginCache.modules) {
            if (m.status != minidaw::ModuleStatus::Failed && m.status != minidaw::ModuleStatus::Crashed) continue;
            if (!header) ImGui::SeparatorText("Modules with problems");
            header = true;
            ImGui::BulletText("%s: %s%s", m.path.c_str(), minidaw::moduleStatusName(m.status),
                              m.error.empty() ? "" : (" - " + m.error).c_str());
        }
    }
    ImGui::End();
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
    d.note = app.projectNote;  // P22
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
// Returns false when a take could not be copied (missing takes are only reported).
bool importIntoProject(AppState& app, const std::filesystem::path& folder, std::vector<minidaw::Track>& tracks)
{
    const minidaw::ImportResult r = minidaw::importTakes(folder, tracks);
    for (const auto& c : r.copied) app.log.info("Copied into the project: " + c);
    for (const auto& m : r.missing) {
        app.log.warning(m);
        app.projectNotes.push_back(m);
    }
    for (const auto& f : r.failed) {
        app.log.error(f);
        app.projectNotes.push_back(f);
    }
    return r.failed.empty();
}

void importIntoProject(AppState& app)
{
    if (!app.projectFolder.empty()) importIntoProject(app, app.projectFolder, app.tracks);
}

// A folder typed by the user: absolute, normal, no trailing separator (N8).
std::filesystem::path cleanFolder(const std::filesystem::path& typed)
{
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(typed, ec);
    if (ec) p = typed;
    p = p.lexically_normal();
    if (p.filename().empty() && p.has_parent_path()) p = p.parent_path();
    return p;
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
void previewRecording(AppState& app, const minidaw::RecordingInfo& rec);  // P17, further down

bool openProject(AppState& app, const std::filesystem::path& typedFolder)
{
    const std::filesystem::path folder = cleanFolder(typedFolder);
    if (app.session.recording()) {
        app.projectDialogError = "Stop the recording first.";
        return false;
    }
    minidaw::ProjectData d;
    std::vector<std::string> warnings;
    std::string err;
    if (!minidaw::loadProject(folder, d, warnings, err)) {
        app.projectDialogError = err;
        // Also in the status line: Recent and the start have no dialog open (N5).
        app.projectNotes.push_back("Could not open the project: " + err);
        app.log.error("Open project: " + err);
        return false;
    }
    refreshInstrumentStates(app);
    saveProjectNow(app);  // the current one first
    stopPlayback(app);
    unloadAllInstruments(app);
    app.projectFolder = folder;
    app.projectNote = d.note;  // P22
    app.showProjectNote = false;  // an open note window belonged to the other project (Y2)
    app.tracks = std::move(d.tracks);
    app.tracksCreated = true;
    app.midiPortsDirty = true;
    app.audio.player().locate(0);
    app.projectNotes = warnings;
    app.projectError.clear();
    for (const auto& w : warnings) app.log.warning("Project: " + w);
    importIntoProject(app);  // a project saved with outside takes (should not happen, but repairable)
    for (auto& t : app.tracks) refreshTakeLength(app, t);
    restoreInstruments(app);  // with their saved state (SPEC-VST-06)
    app.projectSavedText = minidaw::projectToJson(currentProject(app), folder);
    if (!warnings.empty()) {
        // Opening needed repairs: keep the original, then save the repaired
        // state, so nothing skipped disappears without notice (N6).
        std::error_code ec;
        std::filesystem::copy_file(folder / minidaw::kProjectFileName,
                                   folder / (std::string(minidaw::kProjectFileName) + ".bak"),
                                   std::filesystem::copy_options::overwrite_existing, ec);
        const std::string note = ec ? "The project file needed repairs; the original could not be kept (" + ec.message() + ")."
                                    : "The project file needed repairs (see above); the original is kept as project.json.bak.";
        app.projectNotes.push_back(note);
        app.log.warning(note);
        app.projectSavedText.clear();  // the repaired state is written by the next autosave
    }
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
bool createProject(AppState& app, const std::filesystem::path& typedFolder, bool keepTracks)
{
    const std::filesystem::path folder = cleanFolder(typedFolder);
    if (app.session.recording()) {
        app.projectDialogError = "Stop the recording first.";
        return false;
    }
    if (!app.projectFolder.empty() &&
        (folder == app.projectFolder || minidaw::isInsideFolder(folder, app.projectFolder))) {
        app.projectDialogError = "Choose a folder outside the open project.";  // no nested projects (N9)
        return false;
    }
    std::string err;
    if (!minidaw::createProjectFolder(folder, err)) {
        app.projectDialogError = err;
        return false;
    }
    refreshInstrumentStates(app);
    saveProjectNow(app);
    std::vector<minidaw::Track> tracks = keepTracks ? app.tracks : std::vector<minidaw::Track>{};
    if (keepTracks && !importIntoProject(app, folder, tracks)) {
        // A take could not be copied: stay with the current project (N4).
        app.projectDialogError = "Not every take could be copied into the new folder (see the notes below the "
                                 "tracks and the log). Free some disk space and try again.";
        return false;
    }
    stopPlayback(app);
    app.projectFolder = folder;
    app.projectError.clear();
    app.showProjectNote = false;  // Y2
    if (keepTracks) {
        app.tracks = std::move(tracks);
    } else {
        unloadAllInstruments(app);
        app.projectNotes.clear();
        app.projectNote.clear();  // a new project starts without a note (Save as keeps it)
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
    refreshInstrumentStates(app);
    saveProjectNow(app);
    stopPlayback(app);
    app.log.info("Closed project " + minidaw::pathToUtf8(app.projectFolder));
    app.projectFolder.clear();
    app.projectSavedText.clear();
    app.projectNotes.clear();
    app.projectNote.clear();
    app.showProjectNote = false;
    app.projectError.clear();
    unloadAllInstruments(app);
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
        minidaw::app::beginDisabled(app.session.recording());
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
            browseButton(app, AppState::BrowseFor::ProjectFolder, app.projectFolderInput, projectsBase(app),
                         "Choose the folder of a project");
            if (ImGui::Button("Open")) {
                const std::string folder = minidaw::cleanPathText(app.projectFolderInput);
                startBusy(app, "Opening project " + folder, [&app, folder] {
                    if (openProject(app, minidaw::pathFromUtf8(folder))) app.projectDialog = D::None;
                });
            }
        } else {
            ImGui::SetNextItemWidth(20 * em);
            ImGui::InputText("Name", &app.projectNameInput);
            ImGui::SetNextItemWidth(36 * em);
            ImGui::InputText("In folder", &app.projectFolderInput);
            browseButton(app, AppState::BrowseFor::ProjectFolder, app.projectFolderInput, projectsBase(app),
                         "Choose the folder to make the project in");
            const std::string name = minidaw::safeFileName(app.projectNameInput);
            const std::filesystem::path target =
                minidaw::pathFromUtf8(minidaw::cleanPathText(app.projectFolderInput)) / minidaw::pathFromUtf8(name);
            if (!app.projectNameInput.empty()) ImGui::TextWrapped("Project folder: %s", minidaw::pathToUtf8(target).c_str());
            if (app.projectDialog == D::SaveAs) {
                ImGui::TextWrapped("The current tracks are saved there; their takes are copied into the new folder.");
            } else {
                ImGui::TextWrapped("Starts with an empty track list; new takes are recorded into the project folder.");
            }
            minidaw::app::beginDisabled(app.projectNameInput.empty());
            if (ImGui::Button(app.projectDialog == D::New ? "Create" : "Save")) {
                const bool keepTracks = app.projectDialog == D::SaveAs;  // Save as copies the takes
                startBusy(app, (keepTracks ? "Saving the project as " : "Making the project ") + minidaw::pathToUtf8(target),
                          [&app, target, keepTracks] {
                              if (createProject(app, target, keepTracks)) app.projectDialog = D::None;
                          });
            }
            minidaw::app::endDisabled();
        }
        minidaw::app::endDisabled();
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
    for (const auto& t : app.tracks) {
        const float g = minidaw::playbackGain(t, app.tracks);  // with the "Sound from" rule (P11)
        app.audio.player().setGain(t.number, g, g);
        app.midiPlayer.setGain(t.number, g);
        app.audio.instruments().setGain(t.number, g);
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
    // Auto record (P16): the armed tracks are being recorded, so their takes do not play.
    const bool skipArmed = forRecording || app.session.autoMode();
    // A recording played from the timeline stops first: its position is not the song's (crosscheck #12, T1).
    if (!app.previewFolder.empty()) stopPlayback(app);
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
        if (t.take.empty() || (skipArmed && t.armed)) continue;
        // While recording, an armed instrument track's old recorded sound does
        // not play either: the instrument plays live (no doubled sound, R6).
        const minidaw::Track* owner = minidaw::soundOwner(app.tracks, t);
        if (skipArmed && owner != nullptr && owner->armed) continue;
        if (t.kind == minidaw::TrackKind::Audio) {
            clips.push_back({t.number, t.take, t.takeStart});
        } else if (!t.midiOut.empty() && !app.instruments.count(t.number)) {  // instruments: via the rack
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
    const bool instrumentPlays = std::any_of(app.tracks.begin(), app.tracks.end(), [&](const minidaw::Track& t) {
        return !t.take.empty() && app.instruments.count(t.number) && !(skipArmed && t.armed);
    });
    app.overdub = audioStarted > 0 || app.midiPlayer.trackCount() > 0 || instrumentPlays;
    if (forRecording) app.recordStartPosition = position;
    app.playbackForRecording = forRecording;
    for (const auto& w : app.playWarnings) app.log.warning("Playback: " + w);
    char buf[160];
    const auto instrumentTakes = static_cast<std::size_t>(std::count_if(app.tracks.begin(), app.tracks.end(), [&](const minidaw::Track& t) {
        return !t.take.empty() && app.instruments.count(t.number) && !(skipArmed && t.armed);
    }));
    std::snprintf(buf, sizeof buf, "%s from %.3f s: %zu audio, %zu MIDI-out and %zu instrument take(s)",
                  forRecording ? "Recording timeline started" : "Playback started",
                  static_cast<double>(position) / info.sampleRate, audioStarted, app.midiPlayer.trackCount(),
                  instrumentTakes);
    app.log.info(buf);
    return true;
}

void togglePlay(AppState& app)
{
    if (app.session.recording() && !app.session.autoMode()) return;  // Stop the recording with Ctrl+R
    if (app.audio.player().playing()) {
        stopPlayback(app);
        app.log.info("Playback stopped");
    } else {
        startPlayback(app, false);
    }
}

void returnToStart(AppState& app)
{
    if (app.session.recording() && !app.session.autoMode()) return;
    if (!app.previewFolder.empty()) {  // a recording from the timeline: play it again from its start (T1)
        const auto it = std::find_if(app.recordings.begin(), app.recordings.end(),
                                     [&](const minidaw::RecordingInfo& r) { return r.folder == app.previewFolder; });
        if (it != app.recordings.end()) {
            const minidaw::RecordingInfo rec = *it;
            previewRecording(app, rec);
            return;
        }
        stopPlayback(app);
    }
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
    const std::uint64_t length = app.previewFolder.empty() ? songLength(app) : app.previewLength;
    const std::uint64_t end = length + static_cast<std::uint64_t>(std::max<std::int64_t>(app.midiDelayFrames, 0)) +
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
    if (app.session.autoMode()) {  // Auto record is stopped with its own button (S10)
        app.recordError = "auto record is on. Stop it first (Stop auto record).";
        return;
    }
    if (app.session.recording()) {
        const minidaw::SessionResult res = app.session.stop();
        app.recordingsDirty = true;
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
                                                     minidaw::placedAsAudioInput(t), lat);
            refreshTakeLength(app, t);
            char buf[200];
            std::snprintf(buf, sizeof buf, "Track %d plays %s from %.3f s%s", t.number,
                          minidaw::pathToUtf8(file.filename()).c_str(),
                          static_cast<double>(t.takeStart) / app.audio.info().sampleRate,
                          app.overdub ? " (overdub: moved earlier by the latency estimate)" : "");
            app.log.info(buf);
        }
        // The instruments' recorded sound (P11): on their linked tracks, placed
        // like their MIDI takes (in an overdub moved earlier by the MIDI latency
        // estimate, as the notes it was made from: R3).
        for (auto& l : app.tracks) {
            const minidaw::Track* owner = minidaw::soundOwner(app.tracks, l);
            if (owner == nullptr || !owner->armed) continue;
            const std::filesystem::path file = res.folder / minidaw::pathFromUtf8(minidaw::takeFileName(l));
            std::error_code ec;
            if (res.folder.empty() || !std::filesystem::exists(file, ec)) continue;
            l.take = file;
            l.takeStart = minidaw::placeRecordedTake(timeline, app.recordStartPosition, app.overdub,
                                                     minidaw::placedAsAudioInput(l), lat);
            refreshTakeLength(app, l);
            app.log.info("Track " + std::to_string(l.number) + " holds the recorded sound " +
                         minidaw::pathToUtf8(file.filename()));
        }
        stopPlayback(app);
        return;
    }
    if (app.renderTrack != 0) {  // Ctrl+R while rendering (R5)
        app.recordError = "A render is running; record when it has finished (or cancel it).";
        return;
    }
    syncMidiPorts(app, true);
    app.recordError.clear();
    // Armed instrument tracks also record their sound, into their linked track (P11).
    std::vector<int> instrumentTracks;
    for (const auto& t : app.tracks) {
        if (t.armed && t.kind == minidaw::TrackKind::Midi && app.instruments.count(t.number) != 0) {
            instrumentTracks.push_back(t.number);
        }
    }
    for (const int n : instrumentTracks) ensureLinkedTrack(app, n);
    // The playback run gives the timeline (and plays the other takes: overdub).
    startPlayback(app, true);
    minidaw::RecordingSettings recSettings = app.config.recording;
    recSettings.folder = minidaw::pathToUtf8(activeTakesFolder(app));  // into the project, if one is open
    // Only loaded instruments record their sound (R4); linked tracks are not armed themselves (R9).
    const std::vector<minidaw::Track> toRecord =
        minidaw::tracksToRecord(app.tracks, [&](int n) { return app.instruments.count(n) != 0; });
    if (!app.session.start(toRecord, recSettings, app.audio, app.midi, app.recordError)) {
        app.log.error("Not recording: " + app.recordError);
        stopPlayback(app);
        return;
    }
    app.log.info("Recording into " + minidaw::pathToUtf8(app.session.folder()));
    app.recordingsDirty = true;  // the take grows on the timeline while it is recorded (T2)
    app.lastTake.clear();
    for (auto& m : app.inputMeters) m.clearClip();  // CLIP then means: during this take
    for (auto& m : app.outputMeters) m.clearClip();
}

// Auto record (P16, SPEC-REC-08): on until switched off; every time someone
// plays on an armed track, a part is recorded into <takes>/auto/<date time>.
void toggleAuto(AppState& app)
{
    if (app.session.autoMode()) {
        const int parts = app.session.autoPartsDone() + (app.session.autoInPart() ? 1 : 0);
        const minidaw::SessionResult res = app.session.stop();
        app.livePart.clear();
        app.recordingsDirty = true;
        // Losses are always reported (SPEC-REC-05; crosscheck #11, S5).
        std::uint64_t midiLost = 0;
        std::uint64_t disconnects = 0;
        for (const auto& m : res.midi) {
            midiLost += m.lostMessages;
            disconnects += m.disconnects;
        }
        const std::uint64_t lostFrames = app.session.autoLostFrames();
        std::string losses;
        if (lostFrames > 0) losses += std::to_string(lostFrames) + " audio frames lost (silence in their place). ";
        if (midiLost > 0) losses += std::to_string(midiLost) + " MIDI messages lost. ";
        if (disconnects > 0) losses += "A MIDI keyboard was disconnected " + std::to_string(disconnects) + " time(s). ";
        app.log.info("Auto record stopped: " + std::to_string(parts) + " part(s)" +
                     (res.error.empty() ? std::string() : "; " + res.error));
        if (!losses.empty()) app.log.warning("Auto record: " + losses);
        app.recordError = res.error;  // also clears an earlier "stop it first"
        app.lastTake = {"Auto record: " + std::to_string(parts) + " part(s) in " + minidaw::pathToUtf8(res.folder) +
                        " (File > Open take folder)"};
        if (!losses.empty()) app.lastTake.push_back(losses);
        return;
    }
    if (app.session.recording()) return;
    if (app.renderTrack != 0) {
        app.recordError = "A render is running; start auto record when it has finished (or cancel it).";
        return;
    }
    syncMidiPorts(app, true);
    app.recordError.clear();
    std::vector<int> instrumentTracks;
    for (const auto& t : app.tracks) {
        if (t.armed && t.kind == minidaw::TrackKind::Midi && app.instruments.count(t.number) != 0) {
            instrumentTracks.push_back(t.number);
        }
    }
    for (const int n : instrumentTracks) ensureLinkedTrack(app, n);
    if (app.audio.player().playing() && app.playbackForRecording) stopPlayback(app);
    minidaw::RecordingSettings recSettings = app.config.recording;
    recSettings.folder = minidaw::pathToUtf8(activeTakesFolder(app));
    const std::vector<minidaw::Track> toRecord =
        minidaw::tracksToRecord(app.tracks, [&](int n) { return app.instruments.count(n) != 0; });
    if (!app.session.startAuto(toRecord, recSettings, app.audio, app.midi, app.recordError)) {
        app.log.error("Auto record not started: " + app.recordError);
        return;
    }
    app.autoParts.clear();
    app.lastTake.clear();
    // A playback that runs keeps going; the armed tracks' takes leave it.
    // A recording played from the timeline stops (T1).
    if (!app.previewFolder.empty()) {
        stopPlayback(app);
    } else if (app.audio.player().playing()) {
        startPlayback(app, false);
    }
    app.log.info("Auto record on: parts go to " + minidaw::pathToUtf8(app.session.folder()));
}

// Every frame while auto record is on: parts begin and end.
void updateAuto(AppState& app)
{
    if (!app.session.autoMode()) return;
    const minidaw::AutoPartEvent ev = app.session.autoUpdate();
    switch (ev.kind) {
    case minidaw::AutoPartEvent::Kind::Began:
        app.livePart = ev.folder;
        app.recordingsDirty = true;
        app.log.info("Auto record: part " + minidaw::pathToUtf8(ev.folder.filename()) + " began");
        if (ev.cutSeconds > 0.0) {
            char buf[200];
            std::snprintf(buf, sizeof buf,
                          "Auto record: the playing was noticed late (the program was busy); the part's first %.1f s "
                          "could not be kept",
                          ev.cutSeconds);
            app.log.warning(buf);
            app.recordError = buf;
        }
        break;
    case minidaw::AutoPartEvent::Kind::Ended: {
        app.livePart.clear();
        app.recordingsDirty = true;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.1f s", ev.seconds);
        const std::string line = minidaw::pathToUtf8(ev.folder.filename()) + "  (" + buf + ")";
        app.autoParts.insert(app.autoParts.begin(), line);
        if (app.autoParts.size() > 5) app.autoParts.pop_back();
        app.log.info("Auto record: part " + line + " saved in " + minidaw::pathToUtf8(ev.folder));
        break;
    }
    case minidaw::AutoPartEvent::Kind::Failed:
        app.recordError = ev.error;
        app.log.error("Auto record: " + ev.error);
        break;
    case minidaw::AutoPartEvent::Kind::None:
        break;
    }
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
        if (minidaw::app::choice(audioInputLabel(mono).c_str(), t.channels == mono)) t.channels = mono;
    }
    for (unsigned int c = 0; c + 1 < inputs; ++c) {
        const std::vector<unsigned int> pair{c, c + 1};
        if (minidaw::app::choice(audioInputLabel(pair).c_str(), t.channels == pair)) t.channels = pair;
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
            if (minidaw::app::choice(p.name.c_str(), t.midiPort == p.name)) {
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
        if (minidaw::app::choice("All channels", t.midiChannel == 0)) t.midiChannel = 0;
        for (int c = 1; c <= 16; ++c) {
            if (minidaw::app::choice(("Channel " + std::to_string(c)).c_str(), t.midiChannel == c)) t.midiChannel = c;
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

// The recording a note line belongs to now: the take being recorded, or the open auto
// part; empty otherwise.
std::filesystem::path noteFolderNow(const AppState& app)
{
    if (app.session.recording() && !app.session.autoMode()) return app.session.folder();
    if (app.session.autoMode() && app.session.autoInPart()) return app.livePart;
    return {};
}

// Every frame: a note still being typed when its recording ended (Stop, Ctrl+R, the end
// of an auto part) is stored at the moment it was begun, not lost (crosscheck #17, Y3).
void storeUnsentNote(AppState& app)
{
    if (app.markFolder.empty() || noteFolderNow(app) == app.markFolder) return;
    const std::string text = trimSpaces(app.markText);
    if (!text.empty() && app.markSeconds >= 0) {
        std::string err;
        if (minidaw::addRecordingMark(app.markFolder, {app.markSeconds, text}, err)) {
            app.markResult = "The note being typed was saved when the recording ended (at " +
                             clockText(app.markSeconds) + "): " + text;
            app.log.info("Note at " + clockText(app.markSeconds) + " in " + minidaw::pathToUtf8(app.markFolder) + ": " +
                         text + " (saved when the recording ended)");
            app.recordingsDirty = true;
        } else {
            app.markResult = "The note being typed was not saved: " + err;
            app.log.error(app.markResult);
        }
    } else {
        app.markResult.clear();  // "saved" lines belonged to the recording that ended
    }
    app.markText.clear();
    app.markSeconds = -1.0;
    app.markFolder.clear();
}

// A note while recording (P22, SPEC-REC-09): stored at this moment in the recording
// being made (`folder`), `seconds` from its start.
void drawNoteWhileRecording(AppState& app, const std::filesystem::path& folder, double seconds)
{
    if (folder.empty()) return;
    if (folder != app.markFolder) {  // a new recording: a fresh note line
        app.markFolder = folder;
        app.markText.clear();
        app.markResult.clear();
        app.markSeconds = -1.0;
    }
    // The note belongs to the moment it was begun (its first key), not to Enter.
    if (app.markText.empty()) app.markSeconds = -1.0;
    const double at = app.markSeconds >= 0 ? app.markSeconds : seconds;
    const float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Note at %s:", clockText(at).c_str());
    ImGui::SameLine();
    if (app.markFocus) {
        ImGui::SetKeyboardFocusHere();
        app.markFocus = false;
    }
    ImGui::SetNextItemWidth(24 * em);
    const bool enter = ImGui::InputText("##mark", &app.markText, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!app.markText.empty() && app.markSeconds < 0) app.markSeconds = seconds;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Type a note and press Enter: it is kept at the moment you began typing it, and shown on "
                          "the timeline.");
    }
    ImGui::SameLine();
    const std::string text = trimSpaces(app.markText);
    minidaw::app::beginDisabled(text.empty());
    const bool clicked = ImGui::Button("Add note");
    minidaw::app::endDisabled();
    if ((enter || clicked) && !text.empty()) {
        std::string err;
        const double when = app.markSeconds >= 0 ? app.markSeconds : seconds;
        if (minidaw::addRecordingMark(folder, {when, text}, err)) {
            app.markResult = "Note at " + clockText(when) + " saved: " + text;
            app.log.info("Note at " + clockText(when) + " in " + minidaw::pathToUtf8(folder) + ": " + text);
            app.markText.clear();
            app.markSeconds = -1.0;
            app.recordingsDirty = true;
        } else {
            app.markResult = "Note not saved: " + err;
            app.log.error(app.markResult);
        }
    }
    if (enter) app.markFocus = true;  // keep typing the next one (also after an empty Enter, Y7)
    if (!app.markResult.empty()) ImGui::TextWrapped("%s", app.markResult.c_str());  // wrapped (Y7)
}

void drawTransport(AppState& app)
{
    ImGui::SeparatorText("Transport");
    const bool rec = app.session.recording();
    const bool running = app.audio.info().running;
    auto& player = app.audio.player();
    const bool playing = player.playing() && !app.playbackForRecording;
    const unsigned int rate = running ? app.audio.info().sampleRate : app.config.audio.sampleRate;
    // While a recording plays on its own from the timeline (P17): its length.
    const std::uint64_t length = app.previewFolder.empty() ? songLength(app) : app.previewLength;
    const bool autoOn = app.session.autoMode();  // auto record (P16): playback may run meanwhile
    const bool recNormal = rec && !autoOn;

    minidaw::app::beginDisabled(recNormal || !running);
    if (ImGui::Button("|< Start (Home)")) returnToStart(app);
    ImGui::SameLine();
    minidaw::app::beginDisabled(!playing && length == 0);
    if (ImGui::Button(playing ? "Stop (Space)" : "Play (Space)")) togglePlay(app);
    minidaw::app::endDisabled();
    minidaw::app::endDisabled();
    ImGui::SameLine();
    minidaw::app::beginDisabled(autoOn || (!rec && (!running || app.renderTrack != 0)));  // not while rendering (R5)
    if (ImGui::Button(recNormal ? "Stop recording (Ctrl+R)" : "Record (Ctrl+R)")) toggleRecording(app);
    if (!rec && app.renderTrack != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Recording waits until the render has finished (or is cancelled).");
    }
    minidaw::app::endDisabled();
    ImGui::SameLine();
    // Auto record (SPEC-REC-08).
    const bool anyArmed =
        std::any_of(app.tracks.begin(), app.tracks.end(), [](const minidaw::Track& t) { return t.armed; });
    minidaw::app::beginDisabled(recNormal || (!autoOn && (!running || app.renderTrack != 0 || !anyArmed)));
    if (ImGui::Button(autoOn ? "Stop auto record" : "Auto record")) toggleAuto(app);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(autoOn ? "Stop waiting for parts (a part being recorded is finished and saved)."
                                 : "Record everything you play on the armed tracks, without pressing Record:\n"
                                   "a part starts when you play and ends after %d s without playing.\n"
                                   "Each part is saved in its own folder named after the date and time;\n"
                                   "the tracks are not changed.%s",
                          app.config.recording.autoPauseSeconds, anyArmed ? "" : "\nArm a track first.");
    }
    minidaw::app::endDisabled();
    ImGui::SameLine();
    const double pos = rate ? static_cast<double>(player.position()) / rate : 0.0;
    const char* state = autoOn ? (app.session.autoInPart() ? "AUTO: RECORDING" : "AUTO: WAITING")
                        : rec  ? "RECORDING"
                        : playing ? "PLAYING"
                                  : "STOPPED";
    ImGui::Text("%s  %s / %s", state, clockText(pos).c_str(),
                clockText(rate ? static_cast<double>(length) / rate : 0.0).c_str());
    // Everything below has a fixed height, so the track table never moves
    // when lines come and go while recording or after it (user test 2026-11-06, SPEC-UI-10).
    {
        // Position slider: always there, usable while stopped.
        float seconds = static_cast<float>(pos);
        const float rateF = rate > 0 ? static_cast<float>(rate) : 48000.0f;
        minidaw::app::beginDisabled(recNormal || playing || length == 0 || rate == 0);
        ImGui::SetNextItemWidth(30 * ImGui::GetFontSize());
        if (ImGui::SliderFloat("Position (s)", &seconds, 0.0f, std::max(0.1f, static_cast<float>(length) / rateF),
                               "%.1f s")) {
            player.locate(static_cast<std::uint64_t>(std::max(0.0f, seconds) * rateF));
        }
        minidaw::app::endDisabled();
        ImGui::SameLine();
        minidaw::app::beginDisabled(rec);
        if (ImGui::SmallButton("Recording settings...")) {
            app.pendingRecording = app.config.recording;
            app.showRecordingSettings = true;
        }
        minidaw::app::endDisabled();
    }
    const float infoHeight = ImGui::GetTextLineHeightWithSpacing() * 8.0f;  // 8: room for the note line (Y7)
    if (!ImGui::BeginChild("transport-info", ImVec2(0, infoHeight))) {
        ImGui::EndChild();
        return;
    }
    if (player.underrunFrames() > 0 && player.playing()) {
        ImGui::TextWrapped("Note: the disk was too slow for playback (%llu frames played as silence).",
                           static_cast<unsigned long long>(player.underrunFrames()));
    }
    for (const auto& w : app.playWarnings) ImGui::TextWrapped("Note: %s", w.c_str());
    if (autoOn) {
        const int pause = app.config.recording.autoPauseSeconds;
        if (app.session.autoInPart()) {
            ImGui::Text("AUTO RECORD: recording a part, %s so far (quiet for %.1f s; it ends after %d s)",
                        clockText(app.session.autoPartSeconds()).c_str(), app.session.autoQuietSeconds(), pause);
            drawNoteWhileRecording(app, app.livePart, app.session.autoPartSeconds());
        } else {
            ImGui::TextWrapped("AUTO RECORD: waiting. Play on an armed track to start a part; it ends after %d s "
                               "without playing.",
                               pause);
        }
        ImGui::TextWrapped("Parts go to %s", minidaw::pathToUtf8(app.session.folder()).c_str());
        if (app.session.autoLostFrames() > 0 || app.session.midiLost() > 0) {
            ImGui::Text("Lost in parts: %llu audio frames, %llu MIDI messages",
                        static_cast<unsigned long long>(app.session.autoLostFrames()),
                        static_cast<unsigned long long>(app.session.midiLost()));
        }
        const std::string err = app.session.diskError();
        if (!err.empty()) ImGui::TextWrapped("Error: %s", err.c_str());
        if (err != app.loggedRecordingError) {
            if (!err.empty()) app.log.error("Auto record: " + err);
            app.loggedRecordingError = err;
        }
        if (!app.autoParts.empty()) {
            ImGui::Text("Parts saved: %d. Last:", app.session.autoPartsDone());
            for (const auto& line : app.autoParts) {
                ImGui::Bullet();
                ImGui::TextUnformatted(line.c_str());
            }
        }
    } else if (rec) {
        if (app.overdub) ImGui::TextUnformatted("Overdub: the other takes play while the armed tracks record.");
        const double s = app.session.elapsedSeconds();
        const int m = static_cast<int>(s / 60);
        ImGui::Text("RECORDING  %02d:%04.1f   lost frames: %llu   MIDI messages: %zu (lost: %llu)", m, s - 60.0 * m,
                    static_cast<unsigned long long>(app.session.lostFrames()), app.session.midiMessages(),
                    static_cast<unsigned long long>(app.session.midiLost()));
        drawNoteWhileRecording(app, app.session.folder(), s);
        ImGui::TextWrapped("Into: %s", minidaw::pathToUtf8(app.session.folder()).c_str());
        const std::string err = app.session.diskError();
        if (!err.empty()) ImGui::TextWrapped("Error: %s", err.c_str());
        if (err != app.loggedRecordingError) {  // log it at once, not only at Stop (crosscheck #5, K3)
            if (!err.empty()) app.log.error("Recording: " + err);
            app.loggedRecordingError = err;
        }
    } else {
        if (const int n = minidaw::overdubTrackCount(app.tracks); anyArmed && n > 0) {
            ImGui::TextWrapped("Overdub: Record also plays the takes of %d track%s that %s not armed, so you can "
                               "record along with them. Disarm a track to hear its take instead of re-recording it.",
                               n, n == 1 ? "" : "s", n == 1 ? "is" : "are");
        }
        ImGui::TextWrapped("Takes go to %s", minidaw::pathToUtf8(activeTakesFolder(app)).c_str());
    }
    if (!app.recordError.empty()) ImGui::TextWrapped("Not recording: %s", app.recordError.c_str());
    if (!rec && !app.markResult.empty()) ImGui::TextWrapped("%s", app.markResult.c_str());  // a note saved at the end (Y3)
    if (!app.lastTake.empty()) {
        ImGui::TextUnformatted("Last take:");
        for (const auto& line : app.lastTake) {
            ImGui::Bullet();
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
    ImGui::EndChild();
}

// --- Timeline tab (P17, SPEC-PLAY-07) ----------------------------------------------

// Plays one recording on its own from its start (not the tracks): its WAV
// files (audio inputs and instruments' sound) and, for MIDI files of a track
// that plays to a MIDI output, that output.
void previewRecording(AppState& app, const minidaw::RecordingInfo& rec)
{
    if (app.session.recording() && !app.session.autoMode()) return;
    const bool songWasHere = app.previewFolder.empty();
    stopPlayback(app);  // a preview that plays: back to the song's position first
    if (songWasHere) app.songPosition = app.audio.player().position();
    const auto& info = app.audio.info();
    app.playWarnings.clear();
    app.timelineNote.clear();
    if (!info.running) {
        app.timelineNote = "The audio stream is not running.";
        return;
    }
    constexpr int kPreviewTrack = 100000;  // gains of their own, apart from the tracks
    std::vector<minidaw::PlaybackClip> clips;
    std::vector<minidaw::MidiPlaybackTrack> midiTracks;
    std::uint64_t length = 0;
    int id = kPreviewTrack;
    int midiWithoutSound = 0;
    for (const auto& f : rec.files) {
        const std::string ext = minidaw::pathToUtf8(f.extension());
        if (ext == ".wav" || ext == ".WAV") {
            clips.push_back({id, f, 0});
            app.audio.player().setGain(id, 1.0f, 1.0f);
            ++id;
            continue;
        }
        // "NN-name.mid": to the MIDI output of track NN, if it plays to one.
        const std::string name = minidaw::pathToUtf8(f.filename());
        const int number = std::atoi(name.c_str());
        const minidaw::Track* t = findTrack(app, number);
        if (t == nullptr || t->kind != minidaw::TrackKind::Midi || t->midiOut.empty() || app.instruments.count(number)) {
            ++midiWithoutSound;
            continue;
        }
        minidaw::SmfData d;
        std::string err;
        if (!minidaw::readSmf(f, d, err)) continue;
        std::uint64_t end = 0;
        auto events = minidaw::smfToFrames(d, info.sampleRate, &end);
        length = std::max(length, end);
        midiTracks.push_back({id, t->midiOut, std::move(events), 0});
        app.midiPlayer.setGain(id, 1.0f);
        ++id;
    }
    app.audio.player().locate(0);
    app.audio.player().start(clips, 0, info.sampleRate, app.playWarnings);
    const std::int64_t midiDelay = latencyNow(app).midiFrames;
    app.midiDelayFrames = midiDelay;
    app.midiPlayer.start(midiTracks, 0,
                         [&app, midiDelay]() -> std::optional<std::int64_t> {
                             const auto frame = app.audio.currentFrame();
                             if (!frame) return std::nullopt;
                             const auto t = app.audio.player().timelineAt(*frame);
                             if (!t) return std::nullopt;
                             return *t - midiDelay;
                         },
                         app.playWarnings);
    app.previewFolder = rec.folder;
    app.previewLength = std::max(length, static_cast<std::uint64_t>(rec.seconds * info.sampleRate));
    // Files the players skipped (another sample rate, unreadable): in the note and the log (T6).
    for (const auto& w : app.playWarnings) {
        app.log.warning("Timeline: " + w);
        if (!app.timelineNote.empty()) app.timelineNote += "\n";
        app.timelineNote += w;
    }
    if (clips.empty() && midiTracks.empty()) {
        app.timelineNote = "Nothing to hear: it holds only MIDI for an instrument or no output. Load it onto the "
                           "tracks to play it through them.";
    } else if (midiWithoutSound > 0 && clips.empty() && app.timelineNote.empty()) {
        app.timelineNote = "Its MIDI has no output to play to; load it onto the tracks to hear it.";
    }
    app.log.info("Playing the recording " + minidaw::pathToUtf8(rec.folder));
}

// Puts a recording's files on the tracks as their takes (P17): the tracks
// keep their instruments; files without a track get new ones.
void loadRecording(AppState& app, const std::filesystem::path& folder)
{
    if (app.session.recording()) {
        app.timelineNote = "Stop the recording first.";
        return;
    }
    if (app.renderTrack != 0) {  // T12: the render reads a track's take
        app.timelineNote = "A render is running; load when it has finished (or cancel it).";
        return;
    }
    stopPlayback(app);
    std::string err;
    const int placed = minidaw::assignTakeFolder(app.tracks, folder, err);
    if (placed == 0) {
        app.timelineNote = err;
        app.log.warning("Load recording: " + err);
        return;
    }
    app.tracksCreated = true;
    app.midiPortsDirty = true;
    importIntoProject(app);  // a project keeps its own copy of the takes
    for (auto& t : app.tracks) refreshTakeLength(app, t);
    app.audio.player().locate(0);
    app.timelineNote = std::to_string(placed) + " file(s) of " + minidaw::pathToUtf8(folder.filename()) +
                       " are now the tracks' takes (Recording tab).";
    app.log.info("Loaded the recording " + minidaw::pathToUtf8(folder) + " onto the tracks: " + std::to_string(placed) +
                 " file(s)");
}

// --- A recording's name, notes and Delete (P21, SPEC-PLAY-08) ---------------
// A name as shown on a timeline block: at most 40 characters (then "..."), and "##"
// broken up, which ImGui would take as the end of a label (crosscheck #17, Y6).
// The tooltip and the details show the whole name.
std::string blockName(const std::string& name)
{
    std::string out;
    std::size_t chars = 0;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if ((c & 0xC0) != 0x80 && ++chars > 40) {
            out += "...";
            break;
        }
        out += name[i];
        if (name[i] == '#' && i + 1 < name.size() && name[i + 1] == '#') out += ' ';
    }
    return out;
}

void openRecordingEdit(AppState& app, const minidaw::RecordingInfo& r, bool focusNote)
{
    app.recordingEdit = r.folder;
    app.recordingEditName = r.notes.name;
    app.recordingEditNote = r.notes.note;
    app.recordingEditMarks = r.notes.marks;
    app.recordingEditRemoved.clear();
    app.recordingEditUnreadable = false;
    app.recordingEditError = r.notesProblem.empty() ? std::string() : "Its notes could not be read: " + r.notesProblem;
    app.recordingEditFocusNote = focusNote;
    app.recordingEditFocus = true;
}

// Why a recording cannot be deleted now (empty: it can).
std::string deleteRecordingBlocked(const AppState& app, const minidaw::RecordingInfo& r)
{
    if (app.session.recording() || !app.livePart.empty()) return "Not while recording (also not while auto record is on).";
    if (app.audio.player().playing()) return "Stop playing first (its files may be open).";
    if (app.renderTrack != 0) return "Not while a render runs (it writes into a take folder).";
    (void)r;
    return {};
}

void drawRecordingWindows(AppState& app)
{
    const float em = ImGui::GetFontSize();
    // The project's note (P22, SPEC-PRJ-03); saved with the project (autosave).
    if (app.showProjectNote && app.projectFolder.empty()) app.showProjectNote = false;
    if (app.showProjectNote) {
        bool open = true;
        ImGui::SetNextWindowSizeConstraints(ImVec2(36 * em, 0), ImVec2(60 * em, FLT_MAX));
        const std::string title = "Project note - " + minidaw::pathToUtf8(app.projectFolder.filename()) + "###projectnote";
        if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("A note for the whole project (all its recordings):");
            ImGui::InputTextMultiline("##projectnote", &app.projectNoteEdit, ImVec2(40 * em, 10 * ImGui::GetTextLineHeight()));
            if (ImGui::Button("Save")) {
                app.projectNote = app.projectNoteEdit;
                app.log.info("Project note saved");
                open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) open = false;
        }
        ImGui::End();
        if (!open) app.showProjectNote = false;
    }
    // Name and note.
    if (!app.recordingEdit.empty()) {
        bool open = true;
        ImGui::SetNextWindowSizeConstraints(ImVec2(36 * em, 0), ImVec2(60 * em, FLT_MAX));
        const std::string title = "Recording " + minidaw::pathToUtf8(app.recordingEdit.filename()) + "###recordingedit";
        if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            if (app.recordingEditFocus && !app.recordingEditFocusNote) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(30 * em);
            ImGui::InputText("Name", &app.recordingEditName);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Shown on the timeline; the folder keeps its date and time, so its place stays.");
            }
            ImGui::TextUnformatted("Note:");
            if (app.recordingEditFocus && app.recordingEditFocusNote) ImGui::SetKeyboardFocusHere();
            app.recordingEditFocus = false;
            ImGui::InputTextMultiline("##note", &app.recordingEditNote, ImVec2(34 * em, 8 * ImGui::GetTextLineHeight()));
            // Notes made while recording (P22): shown with their moment; Remove takes one out on Save.
            if (!app.recordingEditMarks.empty()) {
                ImGui::TextUnformatted("Notes made while recording:");
                for (std::size_t i = 0; i < app.recordingEditMarks.size(); ++i) {
                    const auto& m = app.recordingEditMarks[i];
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::SmallButton("Remove")) {
                        app.recordingEditRemoved.push_back(m);
                        app.recordingEditMarks.erase(app.recordingEditMarks.begin() + static_cast<std::ptrdiff_t>(i));
                        ImGui::PopID();
                        break;
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s  %s", clockText(m.seconds).c_str(), m.text.c_str());
                    ImGui::PopID();
                }
            }
            const bool saveClicked = ImGui::Button("Save");
            bool replaceClicked = false;
            if (app.recordingEditUnreadable) {
                ImGui::SameLine();
                replaceClicked = ImGui::Button("Replace the unreadable file");
            }
            bool save = saveClicked || replaceClicked;
            // The file again: marks added meanwhile (still recording) stay; removed ones go.
            minidaw::RecordingNotes notes;
            std::string err;
            const bool readable = save && minidaw::loadRecordingNotes(app.recordingEdit, notes, err);
            if (save && !readable && !replaceClicked) {
                // An unreadable file (damaged, or from a newer mini-daw) is only replaced when asked (Y4).
                app.recordingEditUnreadable = true;
                app.recordingEditError = "Not saved: " + err +
                                         ". Saving would replace it and lose what it holds; \"Replace the unreadable "
                                         "file\" does that.";
                save = false;
            }
            if (save) {
                if (!readable) notes = minidaw::RecordingNotes{};  // replaced on request
                for (const auto& gone : app.recordingEditRemoved) {
                    const auto it = std::find_if(notes.marks.begin(), notes.marks.end(), [&](const minidaw::RecordingMark& m) {
                        return m.seconds == gone.seconds && m.text == gone.text;
                    });
                    if (it != notes.marks.end()) notes.marks.erase(it);
                }
                notes.name = trimSpaces(app.recordingEditName);
                notes.note = app.recordingEditNote;
                if (minidaw::saveRecordingNotes(app.recordingEdit, notes, err)) {
                    app.log.info("Recording " + minidaw::pathToUtf8(app.recordingEdit) + ": name \"" + notes.name +
                                 "\", note saved");
                    app.recordingsDirty = true;
                    open = false;
                } else {
                    app.recordingEditError = "Not saved: " + err;
                    app.log.error(app.recordingEditError);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) open = false;
            if (!app.recordingEditError.empty()) ImGui::TextWrapped("%s", app.recordingEditError.c_str());
        }
        ImGui::End();
        if (!open) app.recordingEdit.clear();
    }
    // Delete: moved to the "deleted" folder after this question.
    if (!app.recordingDelete.empty()) {
        const auto it = std::find_if(app.recordings.begin(), app.recordings.end(),
                                     [&](const minidaw::RecordingInfo& r) { return r.folder == app.recordingDelete; });
        if (it == app.recordings.end()) {  // gone meanwhile
            app.recordingDelete.clear();
            return;
        }
        const minidaw::RecordingInfo rec = *it;
        bool open = true;
        ImGui::SetNextWindowSizeConstraints(ImVec2(36 * em, 0), ImVec2(50 * em, FLT_MAX));
        if (ImGui::Begin("Delete recording###recordingdelete", &open,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s%s%s %s, %s long, %zu file(s).", rec.notes.name.c_str(), rec.notes.name.empty() ? "" : ": ",
                               rec.autoPart ? "Auto record part" : "Record take",
                               minidaw::clockLabel(rec.clock, true).c_str(), clockText(rec.seconds).c_str(),
                               rec.files.size());
            const std::filesystem::path target = activeTakesFolder(app) / "deleted";
            ImGui::TextWrapped("It is moved to %s (not shown on the timeline; move it back to get it again).",
                               minidaw::pathToUtf8(target).c_str());
            std::string users;
            for (const auto& t : app.tracks) {
                if (minidaw::isInsideFolder(t.take, rec.folder)) users += (users.empty() ? "" : ", ") + t.name;
            }
            if (!users.empty()) ImGui::TextWrapped("These tracks play it and lose their take: %s.", users.c_str());
            const std::string why = deleteRecordingBlocked(app, rec);
            if (!why.empty()) ImGui::TextWrapped("%s", why.c_str());
            minidaw::app::beginDisabled(!why.empty());
            if (ImGui::Button("Delete")) {
                std::filesystem::path movedTo;
                std::string err;
                if (minidaw::moveRecordingToDeleted(activeTakesFolder(app), rec.folder, movedTo, err)) {
                    int cleared = 0;
                    for (auto& t : app.tracks) {
                        if (!minidaw::isInsideFolder(t.take, rec.folder)) continue;
                        t.take.clear();
                        t.takeStart = 0;
                        refreshTakeLength(app, t);
                        ++cleared;
                    }
                    app.log.info("Recording " + minidaw::pathToUtf8(rec.folder) + " moved to " + minidaw::pathToUtf8(movedTo) +
                                 (cleared ? "; " + std::to_string(cleared) + " track(s) lost their take" : ""));
                    app.timelineNote = (rec.notes.name.empty() ? minidaw::pathToUtf8(rec.folder.filename()) : rec.notes.name) +
                                       " was moved to " + minidaw::pathToUtf8(movedTo) +
                                       (cleared ? " (" + std::to_string(cleared) + " track(s) lost their take)." : ".");
                    if (app.selectedRecording == rec.folder) app.selectedRecording.clear();
                    if (app.recordingEdit == rec.folder) app.recordingEdit.clear();
                    app.recordingsDirty = true;
                    open = false;
                } else {
                    app.recordingDeleteError = "Not deleted: " + err;
                    app.log.error(app.recordingDeleteError);
                }
            }
            minidaw::app::endDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) open = false;
            if (!app.recordingDeleteError.empty()) ImGui::TextWrapped("%s", app.recordingDeleteError.c_str());
        }
        ImGui::End();
        if (!open) app.recordingDelete.clear();
    }
}

void drawTimeline(AppState& app)
{
    ImGui::SeparatorText("Timeline of recordings");
    // The project's note (P22, SPEC-PRJ-03): its first line, and a button to edit it.
    if (!app.projectFolder.empty()) {
        if (ImGui::SmallButton("Project note...")) {
            if (!app.showProjectNote) app.projectNoteEdit = app.projectNote;  // Y2
            app.showProjectNote = true;
        }
        ImGui::SameLine();
        const std::string first = app.projectNote.substr(0, app.projectNote.find('\n'));
        ImGui::TextWrapped("%s: %s%s", minidaw::pathToUtf8(app.projectFolder.filename()).c_str(),
                           first.empty() ? "(no note)" : first.c_str(),
                           first.size() < app.projectNote.size() ? " ..." : "");
    }
    const float em = ImGui::GetFontSize();
    if (app.recordingsDirty) {
        // Folders read before and unchanged are not read again (T4).
        app.recordings = minidaw::listRecordings(activeTakesFolder(app), app.recordings);
        app.recordingsDirty = false;
        ++app.recordingsVersion;
    }
    // The part or take being recorded grows while it is recorded (only its length changes: no copy, T4).
    const bool recNormal = app.session.recording() && !app.session.autoMode();
    // A MIDI-only recording has no file until it ends, so the scan leaves its folder out: add it.
    const auto addLive = [&](const std::filesystem::path& folder, bool autoPart) {
        if (folder.empty()) return;
        for (const auto& r : app.recordings) {
            if (r.folder == folder) return;
        }
        minidaw::RecordingInfo r;
        r.folder = folder;
        r.autoPart = autoPart;
        if (!minidaw::recordingTime(minidaw::pathToUtf8(folder.filename()), autoPart, r.clock)) return;
        r.start = minidaw::continuousTime(r.clock);
        if (std::string err; !minidaw::loadRecordingNotes(folder, r.notes, err)) r.notesProblem = err;  // its marks (Y5)
        const auto at = std::upper_bound(app.recordings.begin(), app.recordings.end(), r,
                                         [](const minidaw::RecordingInfo& a, const minidaw::RecordingInfo& b) {
                                             return a.start < b.start;
                                         });
        app.recordings.insert(at, std::move(r));
        ++app.recordingsVersion;
    };
    addLive(app.livePart, true);
    if (recNormal) addLive(app.session.folder(), false);
    bool anyLive = false;
    for (auto& r : app.recordings) {
        if (!app.livePart.empty() && r.folder == app.livePart) {
            r.seconds = app.session.autoPartSeconds();
            anyLive = true;
        } else if (recNormal && r.folder == app.session.folder()) {
            r.seconds = app.session.elapsedSeconds();
            anyLive = true;
        }
    }
    const std::vector<minidaw::RecordingInfo>& recs = app.recordings;
    if (ImGui::SmallButton("Refresh")) app.recordingsDirty = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(14 * em);
    ImGui::SliderFloat("Zoom (pixels per second)", &app.timelineZoom, 0.5f, 100.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::TextWrapped("From %s. Pauses longer than 10 s are drawn short, with their real length.",
                       minidaw::pathToUtf8(activeTakesFolder(app)).c_str());
    if (recs.empty()) {
        ImGui::TextWrapped("No recordings yet. Record (Ctrl+R) or switch on Auto record and play: they appear here.");
        return;
    }
    const float scale = em / 16.0f;
    const float pps = app.timelineZoom * scale;
    const float minWidth = ImGui::CalcTextSize("> 00:00 00:00 REC").x + 2 * ImGui::GetStyle().FramePadding.x;
    const float gapWidth = 6.0f * em;
    if (anyLive || app.layoutVersion != app.recordingsVersion || app.layoutPps != pps ||
        app.layoutMinWidth != minWidth || app.layoutGapWidth != gapWidth) {
        // A block with a name is as wide as its label (P21).
        std::vector<double> widths(recs.size(), 0.0);
        for (std::size_t i = 0; i < recs.size(); ++i) {
            if (recs[i].notes.name.empty()) continue;
            widths[i] = ImGui::CalcTextSize(("> " + blockName(recs[i].notes.name) + " 00:00 00:00 REC").c_str()).x +
                        2 * ImGui::GetStyle().FramePadding.x;
        }
        app.timelineLayout = minidaw::layoutTimeline(recs, pps, 10.0, gapWidth, minWidth, &widths);
        app.layoutVersion = app.recordingsVersion;
        app.layoutPps = pps;
        app.layoutMinWidth = minWidth;
        app.layoutGapWidth = gapWidth;
    }
    const minidaw::TimelineLayout& layout = app.timelineLayout;
    const float rowH = ImGui::GetFrameHeight() * 1.6f;
    const float labelH = ImGui::GetTextLineHeightWithSpacing();
    const float pad = em;
    const float height = 2 * (labelH + rowH) + labelH + ImGui::GetStyle().ScrollbarSize + 2 * ImGui::GetStyle().WindowPadding.y;
    int doubleClicked = -1;
    if (ImGui::BeginChild("timeline", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        const float scrollX = ImGui::GetScrollX();
        // At the end in the last frame: keep following while a recording grows (T3).
        const bool atEnd = scrollX >= ImGui::GetScrollMaxX() - 1.0f;
        // Only what is in view is drawn (T4).
        const float viewLeft = scrollX - pad;
        const float viewRight = scrollX + ImGui::GetWindowWidth();
        const auto inView = [&](double x, double w) {
            return pad + static_cast<float>(x + w) >= viewLeft && pad + static_cast<float>(x) <= viewRight;
        };
        const float yAuto = labelH;
        const float yTakes = 2 * labelH + rowH;
        ImGui::SetCursorPos(ImVec2(scrollX + pad, 0));
        ImGui::TextUnformatted("Auto record parts");
        ImGui::SetCursorPos(ImVec2(scrollX + pad, labelH + rowH));
        ImGui::TextUnformatted("Record takes");
        for (const auto& g : layout.gaps) {
            if (!inView(g.x, g.width)) continue;
            ImGui::SetCursorPos(ImVec2(pad + static_cast<float>(g.x) + em * 0.3f, labelH + rowH * 0.5f));
            ImGui::TextUnformatted(g.label.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A pause of %s, drawn short", g.label.c_str() + 1);
        }
        for (const auto& b : layout.blocks) {
            if (!inView(b.x, b.width)) continue;
            const minidaw::RecordingInfo& r = recs[b.index];
            const bool selected = r.folder == app.selectedRecording;
            const bool live = (!app.livePart.empty() && r.folder == app.livePart) ||
                              (recNormal && r.folder == app.session.folder());
            const bool playing = !app.previewFolder.empty() && r.folder == app.previewFolder;
            // "Chorus 21:50 0:07": its name (P21), start and length (minutes:seconds).
            const auto len = static_cast<int>(r.seconds + 0.5);
            char timeText[48];
            std::snprintf(timeText, sizeof timeText, "%s %d:%02d%s", minidaw::clockLabel(r.clock, false).c_str(), len / 60,
                          len % 60, live ? " REC" : playing ? " PLAY" : "");
            const std::string label = std::string(selected ? "> " : "") +
                                      (r.notes.name.empty() ? std::string() : blockName(r.notes.name) + " ") + timeText + "##" +
                                      minidaw::pathToUtf8(r.folder);
            ImGui::SetCursorPos(ImVec2(pad + static_cast<float>(b.x), r.autoPart ? yAuto : yTakes));
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            if (ImGui::Button(label.c_str(), ImVec2(static_cast<float>(b.width) - 2, rowH))) {
                app.selectedRecording = r.folder;
            }
            if (selected) ImGui::PopStyleColor();
            // Right-click: Rename, Notes, Delete (P21, SPEC-PLAY-08).
            ImGui::PushID(minidaw::pathToUtf8(r.folder).c_str());  // not the label: it changes when selected
            if (ImGui::BeginPopupContextItem("recording-menu")) {
                app.selectedRecording = r.folder;
                ImGui::TextUnformatted(timeText);
                ImGui::Separator();
                if (ImGui::MenuItem("Rename...")) openRecordingEdit(app, r, false);
                if (ImGui::MenuItem("Notes...")) openRecordingEdit(app, r, true);
                const std::string why = deleteRecordingBlocked(app, r);
                if (ImGui::MenuItem("Delete...", nullptr, false, why.empty())) {
                    app.recordingDelete = r.folder;
                    app.recordingDeleteError.clear();
                }
                if (!why.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s", why.c_str());
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            // Notes made while recording (P22): a tick at their moment; pointing at one shows it.
            const ImVec2 bmin = ImGui::GetItemRectMin();
            const ImVec2 bmax = ImGui::GetItemRectMax();
            const minidaw::RecordingMark* hoveredMark = nullptr;
            const float tick = std::max(3.0f, em * 0.3f);
            for (const auto& m : r.notes.marks) {
                const double share = r.seconds > 0 ? std::clamp(m.seconds / r.seconds, 0.0, 1.0) : 0.0;
                const float mx = bmin.x + 1.0f + static_cast<float>(share) * (bmax.x - bmin.x - 2.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddTriangleFilled(ImVec2(mx - tick, bmin.y), ImVec2(mx + tick, bmin.y), ImVec2(mx, bmin.y + 1.6f * tick),
                                      ImGui::GetColorU32(ImGuiCol_CheckMark));  // Accent
                // A short line (the top third), so the label below stays readable.
                dl->AddLine(ImVec2(mx, bmin.y), ImVec2(mx, bmin.y + (bmax.y - bmin.y) * 0.35f),
                            ImGui::GetColorU32(ImGuiCol_CheckMark), 1.0f);
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                if (std::fabs(mouse.x - mx) <= tick && mouse.y >= bmin.y && mouse.y <= bmax.y &&
                    ImGui::IsWindowHovered()) {
                    hoveredMark = &m;
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                doubleClicked = static_cast<int>(b.index);
            }
            if (ImGui::IsItemHovered()) {
                std::string problems;  // files whose length could not be read (T11)
                for (const auto& pr : r.problems) problems += "\nNot readable: " + pr;
                const std::string named = r.notes.name.empty() ? std::string() : r.notes.name + "\n";
                std::string note = r.notes.note.substr(0, r.notes.note.find('\n'));  // the first line
                if (note.size() > 80) note = note.substr(0, 80) + "...";
                if (!note.empty()) note = "\nNote: " + note;
                ImGui::SetTooltip("%s%s\n%s, %.1f s, %zu file(s)%s%s%s\nClick: select. Double-click: play it. "
                                  "Right-click: rename, notes, delete.",
                                  named.c_str(), minidaw::pathToUtf8(r.folder.filename()).c_str(),
                                  minidaw::clockLabel(r.clock, true).c_str(), r.seconds, r.files.size(),
                                  live ? " (being recorded)" : "", problems.c_str(), note.c_str());
            }
            if (hoveredMark != nullptr) {  // over the block's own tooltip
                ImGui::SetTooltip("Note at %s: %s", clockText(hoveredMark->seconds).c_str(), hoveredMark->text.c_str());
            }
        }
        ImGui::SetCursorPos(ImVec2(pad + static_cast<float>(layout.width) + pad, yTakes + rowH));
        ImGui::Dummy(ImVec2(1, 1));
        // Follow new recordings: show the end when one is added or one is being recorded.
        if (recs.size() != app.recordingsSeen || (anyLive && atEnd)) {
            ImGui::SetScrollX(ImGui::GetScrollMaxX());
            app.recordingsSeen = recs.size();
        }
    }
    ImGui::EndChild();
    if (doubleClicked >= 0) {
        const minidaw::RecordingInfo rec = recs[static_cast<std::size_t>(doubleClicked)];  // a copy: previewing may rescan
        app.selectedRecording = rec.folder;
        previewRecording(app, rec);
    }

    // The selected recording.
    const auto sel = std::find_if(recs.begin(), recs.end(),
                                  [&](const minidaw::RecordingInfo& r) { return r.folder == app.selectedRecording; });
    if (sel == recs.end()) {
        ImGui::TextUnformatted("Click a recording to select it; double-click to play it.");
    } else {
        ImGui::Text("%s%s%s: %s, %s long", sel->notes.name.empty() ? "" : sel->notes.name.c_str(),
                    sel->notes.name.empty() ? "" : " - ", sel->autoPart ? "Auto record part" : "Record take",
                    minidaw::clockLabel(sel->clock, true).c_str(), clockText(sel->seconds).c_str());
        if (!sel->notes.note.empty()) ImGui::TextWrapped("Note: %s", sel->notes.note.c_str());
        for (const auto& m : sel->notes.marks) {  // P22
            ImGui::Bullet();
            ImGui::TextWrapped("%s  %s", clockText(m.seconds).c_str(), m.text.c_str());
        }
        if (!sel->notesProblem.empty()) ImGui::TextWrapped("Its notes could not be read: %s", sel->notesProblem.c_str());
        const bool playingThis = !app.previewFolder.empty() && app.previewFolder == sel->folder &&
                                 app.audio.player().playing();
        minidaw::app::beginDisabled(recNormal);
        if (ImGui::Button(playingThis ? "Stop" : "Play it")) {
            if (playingThis) {
                stopPlayback(app);
            } else {
                previewRecording(app, *sel);
            }
        }
        minidaw::app::endDisabled();
        ImGui::SameLine();
        minidaw::app::beginDisabled(app.session.recording() || app.renderTrack != 0);
        if (ImGui::Button("Load onto the tracks")) {
            const std::filesystem::path folder = sel->folder;
            loadRecording(app, folder);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Its files become the takes of the tracks with the same numbers (the tracks keep their\n"
                              "instruments); files without such a track get new tracks.%s",
                              app.session.recording() ? "\nNot while recording."
                              : app.renderTrack != 0  ? "\nNot while a render runs."
                                                      : "");
        }
        minidaw::app::endDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Name and notes...")) openRecordingEdit(app, *sel, false);
        ImGui::SameLine();
        const std::string why = deleteRecordingBlocked(app, *sel);
        minidaw::app::beginDisabled(!why.empty());
        if (ImGui::Button("Delete...")) {
            app.recordingDelete = sel->folder;
            app.recordingDeleteError.clear();
        }
        if (!why.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
        minidaw::app::endDisabled();
        ImGui::TextWrapped("Folder: %s", minidaw::pathToUtf8(sel->folder).c_str());
        std::string files;
        for (const auto& f : sel->files) files += (files.empty() ? "" : ", ") + minidaw::pathToUtf8(f.filename());
        ImGui::TextWrapped("Files: %s", files.empty() ? "(none yet)" : files.c_str());
    }
    if (!app.timelineNote.empty()) ImGui::TextWrapped("%s", app.timelineNote.c_str());
}

// --- Presets (PL2, SPEC-VST-07a) ---------------------------------------------
minidaw::Vst3Instrument* trackInstrument(AppState& app, int number)
{
    const auto it = app.instruments.find(number);
    return it != app.instruments.end() && it->second && it->second->loaded() ? it->second.get() : nullptr;
}

// Runs `work` on a track's instrument while the audio thread does not use it: taken
// out of the rack (which waits until the audio thread lets go) and put back after,
// as a project's state is set before the rack has the instrument (crosscheck #16, X1).
// The instrument is silent meanwhile.
bool withInstrumentOutOfRack(AppState& app, int number, const std::function<void(minidaw::Vst3Instrument&)>& work)
{
    const auto it = app.instruments.find(number);
    if (it == app.instruments.end() || !it->second) return false;
    std::unique_ptr<minidaw::Vst3Instrument> inst = std::move(it->second);
    app.instruments.erase(it);
    syncRack(app, true);
    work(*inst);
    app.instruments[number] = std::move(inst);
    syncRack(app, true);
    return true;
}

// Reads the programs and preset files of a track's instrument (again if `force`).
void readPresets(AppState& app, const minidaw::Track& t, bool force)
{
    minidaw::Vst3Instrument* inst = trackInstrument(app, t.number);
    if (inst == nullptr) return;
    auto& m = app.presetMenu;
    if (!force && m.track == t.number && m.serial == inst->serial()) return;
    const bool first = m.serial != inst->serial();
    m = AppState::PresetMenu{};
    m.track = t.number;
    m.serial = inst->serial();
    m.readAt = ImGui::GetTime();
    inst->programs(m.programs);
    m.folders = minidaw::presetFolders(inst->vendor(), inst->name(), inst->modulePath());
    m.listing = minidaw::listPresetFiles(m.folders, 2000);
    m.choices = minidaw::presetChoices(m.programs.names, m.listing.files);
    if (first) {  // what this plugin offers, once per load (for questions about a plugin)
        std::string folders;
        for (const auto& f : m.folders.all) folders += (folders.empty() ? "" : ", ") + minidaw::pathToUtf8(f);
        app.log.info("Presets of " + inst->name() + " (" + inst->vendor() + "): " +
                     std::to_string(m.programs.names.size()) + " program(s)" +
                     (m.programs.listName.empty() ? "" : " in list '" + m.programs.listName + "'") + ", " +
                     std::to_string(m.listing.files.size()) + (m.listing.moreFiles ? "+" : "") +
                     " preset file(s) in " + folders);
    }
}

// The menu's current entry for a track: the one chosen last, else (nothing chosen here)
// the plugin's own current program (X2, X11).
int currentPresetChoice(AppState& app, const minidaw::Track& t)
{
    const auto id = app.presetIds.find(t.number);
    minidaw::Vst3Instrument* inst = trackInstrument(app, t.number);
    const int program = t.instrumentPreset.empty() && inst != nullptr ? inst->currentProgram() : -1;
    return minidaw::findPresetChoice(app.presetMenu.choices, id == app.presetIds.end() ? std::string() : id->second,
                                     t.instrumentPreset, program);
}

// Choose entry `index` of the menu for track `number`. A program is set at once; a
// file is loaded through startBusy (a plugin may take a while), out of the rack (X1).
// Allowed while recording (like a knob in the plugin); the log says so (X6).
void choosePreset(AppState& app, int number, std::size_t index)
{
    minidaw::Track* t = findTrack(app, number);
    minidaw::Vst3Instrument* inst = trackInstrument(app, number);
    const auto& m = app.presetMenu;
    if (t == nullptr || inst == nullptr || index >= m.choices.size()) return;
    const minidaw::PresetChoice choice = m.choices[index];
    const std::string during = app.session.recording() ? " (while recording)" : "";
    std::string error;
    if (index < m.programs.names.size()) {
        if (inst->selectProgram(static_cast<int>(index), error)) {
            t->instrumentPreset = choice.label;
            app.presetIds[number] = choice.id;
            app.log.info("Track " + std::to_string(number) + ": program " + choice.label + during);
        } else {
            app.log.error("Track " + std::to_string(number) + ": " + error);
        }
        return;
    }
    const minidaw::PresetFileEntry file = m.listing.files[index - m.programs.names.size()];
    startBusy(app, "Loading preset " + file.name, [&app, number, file, choice, during] {
        minidaw::Track* track = findTrack(app, number);
        if (track == nullptr || trackInstrument(app, number) == nullptr) return;
        bool ok = false;
        std::string err, warning;
        withInstrumentOutOfRack(app, number, [&](minidaw::Vst3Instrument& instrument) {
            ok = instrument.loadPreset(file.path, err, &warning);
        });
        const std::string where = "Track " + std::to_string(number) + ": preset ";
        if (ok) {
            track->instrumentPreset = choice.label;
            app.presetIds[number] = choice.id;
            app.log.info(where + minidaw::pathToUtf8(file.path) + during);
            if (!warning.empty()) app.log.warning(where + file.name + ": " + warning);
        } else {
            app.log.error(where + file.name + " not loaded: " + err);
        }
    });
}

// The Presets button, its menu and < / > (in the Output column, beside Edit).
void drawPresetControls(AppState& app, minidaw::Track& t)
{
    const std::string popup = "presets-" + std::to_string(t.number);
    if (ImGui::SmallButton("Presets##presets")) {
        readPresets(app, t, true);
        ImGui::OpenPopup(popup.c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("The instrument's programs and preset files; Save preset...%s%s",
                          t.instrumentPreset.empty() ? "" : "\nNow: ", t.instrumentPreset.c_str());
    }
    for (int step : {-1, +1}) {
        ImGui::SameLine();
        if (ImGui::SmallButton(step < 0 ? "<##prevpreset" : ">##nextpreset")) {
            // Read again when it is older than a few seconds: files may have come or gone (X9).
            readPresets(app, t, app.presetMenu.track != t.number || ImGui::GetTime() - app.presetMenu.readAt > 3.0);
            const int next = minidaw::stepPresetChoice(app.presetMenu.choices.size(), currentPresetChoice(app, t), step);
            if (next >= 0) {
                choosePreset(app, t.number, static_cast<std::size_t>(next));
            } else {
                app.log.info("Track " + std::to_string(t.number) + ": the instrument has no presets to step through");
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(step < 0 ? "Previous preset" : "Next preset");
    }
    if (!ImGui::BeginPopup(popup.c_str())) return;
    const auto& m = app.presetMenu;
    const int current = currentPresetChoice(app, t);  // one entry is marked (X2)
    const std::string now = !t.instrumentPreset.empty() ? t.instrumentPreset
                            : current >= 0 ? m.choices[static_cast<std::size_t>(current)].label +
                                                 " (the plugin's current program)"
                                           : std::string("no preset chosen here yet");
    ImGui::Text("Track %d: %s", t.number, now.c_str());
    const float lines = static_cast<float>(std::min<std::size_t>(m.choices.size() + 4, 20));
    if (ImGui::BeginChild("list", ImVec2(28 * ImGui::GetFontSize(), lines * ImGui::GetTextLineHeightWithSpacing()))) {
        for (std::size_t index = 0; index < m.choices.size(); ++index) {
            if (index == 0 && !m.programs.names.empty()) {
                ImGui::SeparatorText(("Programs" + (m.programs.listName.empty() ? "" : ": " + m.programs.listName)).c_str());
            }
            const bool isFile = index >= m.programs.names.size();
            if (index == m.programs.names.size() && isFile) ImGui::SeparatorText("Preset files");
            const minidaw::PresetFileEntry* file = isFile ? &m.listing.files[index - m.programs.names.size()] : nullptr;
            const std::string label = m.choices[index].label + (file != nullptr && file->user ? "  (mine)" : "");
            ImGui::PushID(static_cast<int>(index));
            if (minidaw::app::choice(label.c_str(), static_cast<int>(index) == current)) {
                choosePreset(app, t.number, index);
                ImGui::CloseCurrentPopup();
            }
            if (file != nullptr && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", minidaw::pathToUtf8(file->path).c_str());
            ImGui::PopID();
        }
        if (m.listing.moreFiles) ImGui::TextUnformatted("Only the first 2000 preset files are listed.");
        if (m.listing.searchCut) ImGui::TextWrapped("A preset folder is very large: it was not searched to the end.");
        if (m.choices.empty()) {
            ImGui::TextWrapped("This instrument publishes no programs, and there are no preset files in its "
                               "preset folders. Its own window (Edit) may have its presets; with Save preset... "
                               "you keep a sound of your own.");
        }
    }
    ImGui::EndChild();
    ImGui::Separator();
    minidaw::app::beginDisabled(m.folders.user.empty());
    if (ImGui::Button("Save preset...")) {
        minidaw::Vst3Instrument* inst = trackInstrument(app, t.number);
        app.savePresetTrack = t.number;
        app.savePresetSerial = inst != nullptr ? inst->serial() : 0;
        app.savePresetFolders = m.folders;
        app.savePresetName = t.instrumentPreset.empty() ? t.instrumentName : t.instrumentPreset;
        const std::size_t slash = app.savePresetName.rfind('/');
        if (slash != std::string::npos) app.savePresetName = app.savePresetName.substr(slash + 1);
        app.savePresetFor = "\x01";  // made at the first frame of the window
        app.savePresetError.clear();
        ImGui::CloseCurrentPopup();
    }
    minidaw::app::endDisabled();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 28 * ImGui::GetFontSize());
    if (m.folders.user.empty()) {  // X4
        ImGui::TextWrapped("No folder for your own presets: the Documents (Windows) or home folder is not known.");
    } else {
        ImGui::TextWrapped("Saved in %s", minidaw::pathToUtf8(m.folders.user).c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

// Save preset: the instrument's current sound as a .vstpreset in the user's preset folder.
void drawSavePresetWindow(AppState& app)
{
    if (app.savePresetTrack == 0) return;
    minidaw::Track* t = findTrack(app, app.savePresetTrack);
    minidaw::Vst3Instrument* inst = trackInstrument(app, app.savePresetTrack);
    // The window belongs to the instrument it was opened for: another (or a reloaded) one closes it (X10).
    if (t == nullptr || inst == nullptr || inst->serial() != app.savePresetSerial) {
        app.savePresetTrack = 0;
        return;
    }
    bool open = true;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints(ImVec2(36 * em, 0), ImVec2(36 * em, FLT_MAX));
    if (ImGui::Begin("Save preset###savepreset", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Track %d: %s", t->number, inst->name().c_str());
        ImGui::SetNextItemWidth(24 * em);
        const bool enter = ImGui::InputText("Name", &app.savePresetName, ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string name = trimSpaces(app.savePresetName);  // X3: no path cleaning
        if (name != app.savePresetFor) {  // the file and whether it exists: only when the name changes (X10)
            app.savePresetFor = name;
            app.savePresetFile = name.empty() ? std::filesystem::path() : minidaw::userPresetPath(app.savePresetFolders, name);
            std::error_code ec;
            app.savePresetExists = !name.empty() && std::filesystem::exists(app.savePresetFile, ec);
        }
        if (!name.empty()) ImGui::TextWrapped("File: %s", minidaw::pathToUtf8(app.savePresetFile).c_str());
        if (app.savePresetExists) ImGui::TextUnformatted("A preset with this name exists: Save replaces it.");
        const bool canSave = !name.empty() && !app.savePresetFolders.user.empty();  // one rule for button and Enter (X4)
        minidaw::app::beginDisabled(!canSave);
        const bool clicked = ImGui::Button(app.savePresetExists ? "Replace" : "Save");
        minidaw::app::endDisabled();
        if (canSave && (clicked || enter)) {
            std::string err;
            if (inst->savePreset(app.savePresetFile, err)) {
                // The label is the file's name, so the menu finds it again (X3).
                const std::string label = minidaw::pathToUtf8(app.savePresetFile.stem());
                t->instrumentPreset = label;
                app.presetIds[t->number] = "f:" + minidaw::pathToUtf8(app.savePresetFile);
                app.log.info("Track " + std::to_string(t->number) + ": preset saved as " +
                             minidaw::pathToUtf8(app.savePresetFile));
                app.presetMenu = AppState::PresetMenu{};  // read again next time
                open = false;
            } else {
                app.savePresetError = "Not saved: " + err;
                app.log.error(app.savePresetError);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) open = false;
        if (!app.savePresetError.empty()) ImGui::TextWrapped("%s", app.savePresetError.c_str());
    }
    ImGui::End();
    if (!open) app.savePresetTrack = 0;
}

// --- Track actions, for the buttons and for scripts (P25a) ---------------------

// A click on a control that is disabled does nothing; the log says why, so a
// "nothing happens" can be traced later (user's Windows test 2026-11-10).
// Call right after the control.
void logRefusedClick(AppState& app, bool disabled, const std::string& what, const std::string& why)
{
    if (!disabled || !ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    app.log.info(what + ": " + why);
}

// Adds an audio or MIDI track like the Add buttons. Returns its number, or 0 with `why`.
int addTrack(AppState& app, minidaw::TrackKind kind, std::string& why)
{
    if (app.session.recording()) {
        why = "not while recording";
        return 0;
    }
    minidaw::Track t;
    t.number = minidaw::nextTrackNumber(app.tracks);
    if (kind == minidaw::TrackKind::Audio) {
        if (app.audio.info().inputChannels == 0) {
            why = "audio tracks need an audio input device";
            return 0;
        }
        t.name = "Audio " + std::to_string(t.number);
        t.channels = {0};
    } else {
        t.kind = minidaw::TrackKind::Midi;
        t.name = "MIDI " + std::to_string(t.number);
        if (!app.scan.midiInputs.empty()) t.midiPort = app.scan.midiInputs.front().name;
    }
    app.tracks.push_back(t);
    app.log.info("Track " + std::to_string(t.number) + " \"" + t.name + "\" added");
    return t.number;
}

// Why no track can be removed now (the X buttons are disabled), or empty.
std::string whyNoTrackRemove(const AppState& app)
{
    if (app.session.recording()) return "not while recording";
    if (app.audio.player().playing()) return "not while playing (its take would keep playing)";  // M4
    return {};
}

bool removeTrackNow(AppState& app, int number, std::string& why)
{
    why = whyNoTrackRemove(app);
    if (!why.empty()) return false;
    const auto it = std::find_if(app.tracks.begin(), app.tracks.end(),
                                 [number](const minidaw::Track& t) { return t.number == number; });
    if (it == app.tracks.end()) {
        why = "there is no track " + std::to_string(number);
        return false;
    }
    if (it->kind == minidaw::TrackKind::Midi) app.midiPortsDirty = true;
    const auto index = static_cast<std::size_t>(it - app.tracks.begin());
    app.log.info("Track " + std::to_string(number) + " \"" + it->name + "\" removed");
    if (const minidaw::Track* l = minidaw::linkedSoundTrack(app.tracks, number)) {  // BB3
        app.log.info("Track " + std::to_string(l->number) + " \"" + l->name + "\" is now a normal audio track");
    }
    unloadInstrument(app, number);
    // Links stay consistent: a removed MIDI track's linked track becomes a normal
    // audio track; removing a linked track sets "Sound from" back to the instrument (R2).
    minidaw::removeTrack(app.tracks, index);
    return true;
}

// A MIDI track to a MIDI output (`port`; empty = none) instead of its
// instrument. True if it had an instrument (then call dropEmptyLinkedTrack
// once app.tracks may change).
bool setMidiOutput(AppState& app, minidaw::Track& t, const std::string& port)
{
    const bool hadInstrument = !t.instrumentClass.empty();
    if (!hadInstrument && t.midiOut == port) return false;  // already so: no line, nothing to do (BB1)
    app.log.info("Track " + std::to_string(t.number) + ": output " +
                 (port.empty() ? std::string("(no MIDI output)") : "MIDI output \"" + port + "\"") +
                 (hadInstrument ? " instead of the instrument " + t.instrumentName : std::string()));
    unloadInstrument(app, t.number);
    t.instrumentClass.clear();
    t.instrumentPreset.clear();
    t.midiOut = port;
    return hadInstrument;
}

// An empty sound track goes with its instrument; one with a take stays (and is kept while a
// recording or a render of this track may still fill it).
void dropEmptyLinkedTrack(AppState& app, int number)
{
    if (!app.session.recording() && app.renderTrack != number && minidaw::removeUnusedLinkedTrack(app.tracks, number)) {
        app.log.info("Track " + std::to_string(number) + ": its empty sound track was removed with the instrument");
    }
}

// Chooses instrument `cls` for MIDI track `number`, as in its output list:
// loading a plugin can take seconds, so the busy message comes first (PL1,
// SPEC-UI-16). False if other slow work is still to do (logged).
bool requestInstrument(AppState& app, int number, const minidaw::PluginClass& cls)
{
    if (app.busy) {
        startBusy(app, "Loading instrument " + cls.name, [] {});  // logs that it was not started
        return false;
    }
    app.log.info("Track " + std::to_string(number) + ": instrument " + instrumentLabel(cls) + " chosen");
    startBusy(app, "Loading instrument " + cls.name, [&app, number, cls] {
        minidaw::Track* track = findTrack(app, number);
        if (track == nullptr) {
            app.log.warning("Instrument not loaded: track " + std::to_string(number) + " is gone");
            return;
        }
        // As the list it was chosen in: not while recording or rendering this track (W4).
        if (app.session.recording() || app.renderTrack == number) {
            app.log.warning("Instrument not changed: track " + std::to_string(number) + " is recording or rendering");
            return;
        }
        chooseInstrument(app, *track, cls);
        const bool loaded = app.instruments.count(number) != 0;
        if (app.script) app.script->busyResult = loaded && track->instrumentClass == cls.classId;  // for a script (AA2)
        // Its sound track appears at once, silent (SPEC-VST-03a; user test 2026-11-05).
        if (loaded) ensureLinkedTrack(app, number);
    });
    return true;
}

void setArm(AppState& app, minidaw::Track& t, bool armed)
{
    if (t.armed != armed) app.log.info("Track " + std::to_string(t.number) + (armed ? " armed" : " disarmed"));
    t.armed = armed;
    if (t.kind == minidaw::TrackKind::Midi) app.midiPortsDirty = true;  // its port opens when armed
}

void drawTracksPanel(AppState& app)
{
    ImGui::SeparatorText("Tracks");
    const bool rec = app.session.recording();
    const unsigned int inputs = app.audio.info().inputChannels;
    minidaw::app::beginDisabled(rec);
    minidaw::app::beginDisabled(inputs == 0);
    std::string unused;
    if (ImGui::Button("Add audio track")) addTrack(app, minidaw::TrackKind::Audio, unused);
    logRefusedClick(app, rec || inputs == 0, "Add audio track not done",
                    rec ? "not while recording" : "audio tracks need an audio input device");
    minidaw::app::endDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Add MIDI track")) addTrack(app, minidaw::TrackKind::Midi, unused);
    logRefusedClick(app, rec, "Add MIDI track not done", "not while recording");
    minidaw::app::endDisabled();
    if (inputs == 0) {
        ImGui::SameLine();
        ImGui::TextUnformatted("(audio tracks need an audio input device: Settings > Audio devices)");
    }
    if (app.tracks.empty()) ImGui::TextUnformatted("No tracks. Add an audio or MIDI track.");
    if (!app.tracks.empty()) {  // always shown, so arming the first track does not move the table
        ImGui::TextWrapped("Tick Arm on each track to record, then press Record (Ctrl+R). A MIDI track's port "
                           "opens when it is armed. Tracks that are not armed play along (overdub).");
    }

    const float em = ImGui::GetFontSize();
    // Every column fits its content (fixed widths, nothing sized from the space left),
    // and a narrow window gets a horizontal scrollbar instead of cut-off controls (PL1).
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_ScrollX;
    int removeIndex = -1;
    int renderRequest = 0;  // a MIDI track whose Render was clicked (handled after the table)
    // An empty linked sound track removed when the output changes (after the table: it changes app.tracks).
    int unlinkRequest = 0;
    const bool hasOutput = app.audio.info().running && app.audio.info().outputChannels > 0;
    bool monitoring = false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowH = ImGui::GetFrameHeight() + 2.0f * style.CellPadding.y;
    // With ScrollX the table has a fixed height: counted here (every cell is at most one
    // frame high), plus what the last frame measured beyond that if a cell is taller (W8).
    const float tableH = ImGui::GetTextLineHeight() + 2.0f * style.CellPadding.y +
                         rowH * static_cast<float>(app.tracks.size()) + style.ScrollbarSize + 4.0f + app.trackTableExtra;
    const ImGuiID tableId = ImGui::GetID("tracks");
    if (!app.tracks.empty() && ImGui::BeginTable("tracks", 10, flags, ImVec2(0.0f, tableH))) {
        ImGui::TableSetupScrollFreeze(4, 0);  // #, Arm, Monitor and Name stay when scrolled sideways
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
        ImGui::TableSetupColumn("Output");
        ImGui::TableSetupColumn("Take (plays)");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < app.tracks.size(); ++i) {
            minidaw::Track& t = app.tracks[i];
            ImGui::PushID(t.number);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", t.number);
            ImGui::TableNextColumn();
            // A linked sound track (P11) records with its MIDI track: no arm or monitor of its own.
            minidaw::Track* owner = t.instrumentOf > 0 ? findTrack(app, t.instrumentOf) : nullptr;
            const bool linked = owner != nullptr;
            if (linked) {
                ImGui::TextUnformatted(owner->armed ? "with MIDI" : "-");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Records the sound of track %d's instrument when track %d is armed.", owner->number,
                                      owner->number);
                }
            } else {
                minidaw::app::beginDisabled(rec);
                bool armed = t.armed;
                if (ImGui::Checkbox("##arm", &armed)) setArm(app, t, armed);
                logRefusedClick(app, rec, "Track " + std::to_string(t.number) + ": Arm not changed",
                                "not while recording");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Armed tracks record when you press Record (Ctrl+R).\n"
                                      "Tracks that are not armed play their takes meanwhile, so you can record "
                                      "along with them (overdub).");
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(t.armed ? "armed" : "-");  // not by colour alone (SPEC-UI-07)
                minidaw::app::endDisabled();
            }
            ImGui::TableNextColumn();
            if (linked) {
                ImGui::TextUnformatted("-");
            } else if (t.kind == minidaw::TrackKind::Audio) {  // can change while recording (SPEC-MON-01)
                minidaw::app::beginDisabled(!hasOutput);
                ImGui::Checkbox("##mon", &t.monitor);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Hear this input on the output while the track is armed.\n"
                                      "Use headphones: with speakers a microphone can feed back.");
                }
                minidaw::app::endDisabled();
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
            minidaw::app::beginDisabled(rec);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(8 * em);
            ImGui::InputText("##name", &t.name);
            ImGui::TableNextColumn();
            if (linked) {
                // "Sound from" (SPEC-VST-03a): the instrument plays the MIDI take, or this recording plays.
                ImGui::SetNextItemWidth(14 * em);
                const std::string instr = "Sound from: instrument (track " + std::to_string(owner->number) + ")";
                const char* recText = "Sound from: this recording";
                if (ImGui::BeginCombo("##soundfrom", owner->soundFromRecording ? recText : instr.c_str())) {
                    if (minidaw::app::choice(instr.c_str(), !owner->soundFromRecording)) owner->soundFromRecording = false;
                    minidaw::app::beginDisabled(t.take.empty());
                    if (minidaw::app::choice(recText, owner->soundFromRecording)) owner->soundFromRecording = true;
                    minidaw::app::endDisabled();
                    ImGui::EndCombo();
                } else if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("What plays for track %d: its instrument playing the MIDI take (this track is "
                                      "then silent), or this recorded sound (the instrument then gets no take MIDI;\n"
                                      "playing live on the armed track still sounds).",
                                      owner->number);
                }
            } else if (t.kind == minidaw::TrackKind::Audio) {
                ImGui::SetNextItemWidth(9.5f * em);
                audioInputCombo(t, inputs);
            } else {
                // A fixed width (the full name is the tooltip): sized from the space left,
                // the list would widen the column every frame (PL1).
                ImGui::SetNextItemWidth(10.0f * em);
                if (midiPortCombo(t, app.scan) && t.armed) app.midiPortsDirty = true;
            }
            minidaw::app::endDisabled();
            ImGui::TableNextColumn();
            if (linked) {
                ImGui::TextUnformatted(owner->soundFromRecording ? "plays this recording" : "silent (instrument plays)");
            } else if (t.kind == minidaw::TrackKind::Audio) {
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
            // Output: for MIDI the instrument or MIDI output it plays to (SPEC-PLAY-03), with
            // Edit and Render; its own column so it is never squeezed by the take name (PL1).
            ImGui::TableNextColumn();
            if (t.kind != minidaw::TrackKind::Midi) {
                ImGui::TextUnformatted("-");
            } else {
                // Where the track's MIDI goes: a MIDI output, or a VST3 instrument (P10).
                const bool hasInst = !t.instrumentClass.empty();
                const bool instLoaded = app.instruments.count(t.number) != 0;
                const bool editorOpen = app.pluginWindows.isOpen(t.number);
                const char* editLabel = editorOpen ? "Close##editor" : "Edit##editor";
                // Before the output list, so it stays visible in a narrow column.
                // The instrument's own window: sounds, presets (SPEC-VST-05a).
                if (instLoaded) {
                    minidaw::app::beginDisabled(!minidaw::app::PluginWindows::supported());
                    const bool editPressed = ImGui::SmallButton(editLabel);
                    logRefusedClick(app, !minidaw::app::PluginWindows::supported(),
                                    "Track " + std::to_string(t.number) + ": Edit not done",
                                    "the instrument's own window is not available on this system");
                    if (editPressed) {
                        if (editorOpen) {
                            app.pluginWindows.close(t.number);
                        } else {
                            openPluginWindow(app, t);
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        ImGui::SetTooltip("%s", !minidaw::app::PluginWindows::supported()
                                                    ? "The instrument's own window is not available on this system yet."
                                                : editorOpen ? "Close the instrument's window."
                                                             : "Open the instrument's own window (sounds, presets). It "
                                                               "can stay open while you play.");
                    }
                    minidaw::app::endDisabled();
                    ImGui::SameLine();
                    drawPresetControls(app, t);  // PL2: also where the plugin's own window is not available
                    ImGui::SameLine();
                    // Render the take through the instrument (SPEC-VST-04a), or its progress.
                    if (app.renderTrack == t.number) {
                        ImGui::Text("Rendering %d %%", static_cast<int>(app.renderer.progress() * 100));
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Cancel##render")) app.renderer.cancel();
                        ImGui::SameLine();
                    } else if (!t.take.empty()) {
                        minidaw::app::beginDisabled(app.renderTrack != 0 || rec);
                        if (ImGui::SmallButton("Render##render")) renderRequest = t.number;
                        logRefusedClick(app, app.renderTrack != 0 || rec,
                                        "Track " + std::to_string(t.number) + ": Render not started",
                                        rec ? "not while recording" : "another render is running");
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Render this track's MIDI take through its instrument to a WAV file, "
                                              "faster than real time.\nThe sound goes on its linked track (\"Sound "
                                              "from\" chooses what plays). Playing goes on meanwhile.");
                        }
                        minidaw::app::endDisabled();
                        ImGui::SameLine();
                    }
                }
                ImGui::SetNextItemWidth(14.0f * em);
                const std::string out = hasInst ? "Instrument: " + t.instrumentName + (instLoaded ? "" : " (not loaded)")
                                        : t.midiOut.empty() ? std::string("(no MIDI output)")
                                                            : t.midiOut;
                if (ImGui::BeginCombo("##out", out.c_str())) {
                    if (minidaw::app::choice("(no MIDI output)", !hasInst && t.midiOut.empty())) {
                        if (setMidiOutput(app, t, std::string())) unlinkRequest = t.number;
                    }
                    for (const auto& name : app.midiPlayer.outputPorts()) {
                        if (minidaw::app::choice(name.c_str(), !hasInst && t.midiOut == name)) {
                            if (setMidiOutput(app, t, name)) unlinkRequest = t.number;
                        }
                    }
                    ImGui::SeparatorText("Instruments (VST3)");
                    const auto list = app.pluginCache.instruments();
                    if (list.empty()) ImGui::TextUnformatted("None found yet: Settings > Instruments > Scan");
                    for (const auto& c : list) {
                        ImGui::PushID(c.classId.c_str());
                        if (minidaw::app::choice(instrumentLabel(c).c_str(), t.instrumentClass == c.classId)) {
                            requestInstrument(app, t.number, c);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                } else if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Where this track's MIDI goes (live when armed, and its take when playing): %s",
                                      out.c_str());
                }
            }
            // Take (SPEC-PLAY-01a): the file name; the full path as the tooltip.
            ImGui::TableNextColumn();
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
            const std::string noRemove = whyNoTrackRemove(app);
            minidaw::app::beginDisabled(!noRemove.empty());
            if (ImGui::SmallButton("X")) removeIndex = static_cast<int>(i);
            logRefusedClick(app, !noRemove.empty(), "Track " + std::to_string(t.number) + " not removed", noRemove);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", noRemove.empty() ? "Remove this track"
                                                         : ("Remove this track: " + noRemove).c_str());
            }
            minidaw::app::endDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
        // The rows' real bottom: if it is below the counted height, grow next frame (logged once).
        if (const ImGuiTable* table = ImGui::TableFindByID(tableId)) {
            const float need = table->RowPosY2 - table->OuterRect.Min.y + style.ScrollbarSize + 4.0f;
            const float counted = tableH - app.trackTableExtra;
            const float extra = std::max(0.0f, need - counted);  // also shrinks again (smaller text)
            if (extra > 0.5f && app.trackTableExtra <= 0.5f) {
                static bool logged = false;
                if (!logged) {
                    app.log.info("Track table: a row is taller than counted; the table grows to fit");
                    logged = true;
                }
            }
            app.trackTableExtra = extra > 0.5f ? extra : 0.0f;
        }
    }
    if (monitoring) {
        const auto& info = app.audio.info();
        ImGui::TextWrapped("Monitoring: what you hear is delayed by at least two buffers (%.1f ms) plus the "
                           "driver's latency. Use headphones to avoid feedback.",
                           2000.0 * info.bufferFrames / info.sampleRate);
    }
    if (renderRequest != 0) startRender(app, renderRequest);
    if (removeIndex >= 0) {
        std::string why;
        removeTrackNow(app, app.tracks[static_cast<std::size_t>(removeIndex)].number, why);
    }
    if (unlinkRequest != 0) dropEmptyLinkedTrack(app, unlinkRequest);
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
        browseButton(app, AppState::BrowseFor::RecordingFolder, p.folder, minidaw::recordingFolder(app.config.recording),
                     "Choose the folder for recordings");
        ImGui::TextWrapped("Empty = %s", minidaw::pathToUtf8(minidaw::defaultRecordingFolder()).c_str());
        const char* timingText = p.midiTiming == minidaw::MidiTiming::Smpte ? "SMPTE, 1 ms ticks"
                                                                            : "960 PPQ at 120 BPM (default)";
        ImGui::SetNextItemWidth(20 * em);
        if (ImGui::BeginCombo("MIDI file timing", timingText)) {
            if (minidaw::app::choice("960 PPQ at 120 BPM (default)", p.midiTiming == minidaw::MidiTiming::Ppq))
                p.midiTiming = minidaw::MidiTiming::Ppq;
            if (minidaw::app::choice("SMPTE, 1 ms ticks", p.midiTiming == minidaw::MidiTiming::Smpte))
                p.midiTiming = minidaw::MidiTiming::Smpte;
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Force takes to disk every second (survives power loss)", &p.syncToDisk);
        // Auto record (SPEC-REC-08).
        ImGui::SetNextItemWidth(8 * em);
        if (ImGui::InputInt("Auto record: a part ends after this many quiet seconds", &p.autoPauseSeconds, 1, 5)) {
            p.autoPauseSeconds = std::clamp(p.autoPauseSeconds, 1, 60);
        }
        ImGui::SetNextItemWidth(8 * em);
        if (ImGui::InputInt("Auto record: an audio input counts as playing from (dBFS)", &p.autoThresholdDb, 1, 5)) {
            p.autoThresholdDb = std::clamp(p.autoThresholdDb, -60, -20);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("-60 (very sensitive) to -20 dBFS. Raise it if noise on the input starts parts;\n"
                              "MIDI notes always count.");
        }
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
        minidaw::app::beginDisabled(app.session.recording());
        if (ImGui::Button("Apply")) {
            p.folder = minidaw::cleanPathText(p.folder);  // quotes / spaces around a pasted path (V8)
            app.config.recording = p;
            saveConfig(app);
        }
        minidaw::app::endDisabled();
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
        minidaw::app::beginDisabled(file.empty());
        if (ImGui::Button("Open log folder")) {
            if (!openFolder(file.parent_path())) app.log.warning("The log folder could not be opened.");
        }
        minidaw::app::endDisabled();
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
    unloadAllInstruments(app);    // the old tracks' instruments
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
        minidaw::app::beginDisabled(rec);
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
        browseButton(app, AppState::BrowseFor::TakeFolder, app.takeFolderInput, activeTakesFolder(app),
                     "Choose a take folder");
        if (ImGui::Button("Open") && !app.takeFolderInput.empty()) {
            openTakeFolder(app, minidaw::pathFromUtf8(minidaw::cleanPathText(app.takeFolderInput)));
        }
        minidaw::app::endDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) app.showTakeBrowser = false;
        ImGui::SameLine();
        ImGui::TextUnformatted("(double-click a take to open it)");
        if (!app.takeBrowserError.empty()) ImGui::TextWrapped("%s", app.takeBrowserError.c_str());
    }
    ImGui::End();
}

// The folder chosen with a Browse... button goes into its field (P19); an
// instrument folder is added at once (with the same checks as Add folder).
void updateFolderChooser(AppState& app)
{
    std::filesystem::path chosen;
    const bool got = app.folderChooser.update(chosen);
    if (const std::string note = app.folderChooser.takeNote(); !note.empty()) app.log.warning("Folder dialog: " + note);
    if (!got) return;
    const std::string text = minidaw::pathToUtf8(chosen);
    switch (app.browseFor) {
    case AppState::BrowseFor::InstrumentFolder:
        app.instrumentFolderInput = text;
        addInstrumentFolder(app);
        break;
    case AppState::BrowseFor::ProjectFolder: app.projectFolderInput = text; break;
    case AppState::BrowseFor::RecordingFolder: app.pendingRecording.folder = text; break;
    case AppState::BrowseFor::TakeFolder: app.takeFolderInput = text; break;
    case AppState::BrowseFor::None: break;
    }
    app.browseFor = AppState::BrowseFor::None;
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

// Shows the main window, which was kept hidden while mini-daw started, at its
// place: maximised at once if it was (SPEC-UI-09). Showing it first and then
// maximising showed the normal-size window on a black screen for the start-up
// time (user test 2026-11-06).
void showMainWindow(GLFWwindow* window, bool maximized)
{
#if defined(_WIN32)
    if (maximized) {
        // One step: maximised, with the saved normal place kept for un-maximising.
        HWND hwnd = glfwGetWin32Window(window);
        WINDOWPLACEMENT wp{};
        wp.length = sizeof wp;
        if (GetWindowPlacement(hwnd, &wp)) {
            wp.showCmd = SW_SHOWMAXIMIZED;
            if (SetWindowPlacement(hwnd, &wp)) return;
        }
    }
#else
    if (maximized) glfwMaximizeWindow(window);  // while hidden: the window manager shows it maximised
#endif
    glfwShowWindow(window);
    if (maximized && !glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) glfwMaximizeWindow(window);
}

// --- Script files (P25a, SPEC-RUN-02) -----------------------------------------

// The report's first line: version, script, time, config.
std::string scriptReportHeader(const std::filesystem::path& script, const std::filesystem::path& config)
{
    const std::time_t now = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return std::string(minidaw::kProductName) + ' ' + minidaw::kVersion + ", script " + minidaw::pathToUtf8(script) +
           ", " + when + ", config " + minidaw::pathToUtf8(config) + '\n';
}

// What a script can check: mini-daw's state as JSON (expect, wait-for, state).
nlohmann::json scriptState(AppState& app)
{
    using nlohmann::json;
    const auto& info = app.audio.info();
    json tracks = json::object();
    for (const minidaw::Track& t : app.tracks) {
        json j;
        j["name"] = t.name;
        j["kind"] = t.kind == minidaw::TrackKind::Midi ? "midi" : "audio";
        j["armed"] = t.armed;
        j["mute"] = t.mute;
        j["solo"] = t.solo;
        j["volumeDb"] = t.volumeDb;
        j["take"] = t.take.empty() ? std::string() : minidaw::pathToUtf8(t.take.filename());
        if (t.kind == minidaw::TrackKind::Midi) {
            j["midiInput"] = t.midiPort;
            j["midiOutput"] = t.midiOut;
            j["instrument"] = t.instrumentClass.empty() ? std::string() : t.instrumentName;
            j["instrumentClass"] = t.instrumentClass;
            j["loaded"] = app.instruments.count(t.number) != 0;
            j["editorOpen"] = app.pluginWindows.isOpen(t.number);
            j["preset"] = t.instrumentPreset;
        } else {
            j["soundOf"] = t.instrumentOf;  // 0: not a linked sound track
            j["soundFrom"] = t.soundFromRecording ? "recording" : "instrument";
        }
        tracks[std::to_string(t.number)] = std::move(j);
    }
    json instruments = json::array();
    for (const auto& c : app.pluginCache.instruments()) instruments.push_back(instrumentLabel(c));
    const unsigned int rate = info.sampleRate ? info.sampleRate : 48000;
    return json{
        {"project", minidaw::pathToUtf8(app.projectFolder)},
        {"busy", app.busy ? json(app.busy->message) : json(nullptr)},
        {"scanning", app.scanning},
        {"recording", app.session.recording()},
        {"autoRecord", app.session.autoMode()},
        {"playing", app.audio.player().playing()},
        {"position", static_cast<double>(app.audio.player().position()) / rate},
        {"rendering", app.renderTrack},
        {"audio",
         {{"running", info.running},
          {"api", info.api},
          {"input", info.inputName},
          {"output", info.outputName},
          {"sampleRate", info.sampleRate},
          {"bufferFrames", info.bufferFrames}}},
        {"midiOutputs", app.midiPlayer.outputPorts()},
        {"instruments", instruments},
        {"trackCount", app.tracks.size()},
        {"tracks", tracks},
        {"log", {{"warnings", app.log.warningCount()}, {"errors", app.log.errorCount()}}},
    };
}

// The log lines written since the last call, into the report (indented).
void scriptCopyLog(AppState& app)
{
    ScriptRun& sr = *app.script;
    const std::uint64_t total = app.log.lineCount();
    if (total == sr.logSeen) return;
    const std::vector<minidaw::LogEntry> recent = app.log.recent();
    const std::uint64_t fresh = std::min<std::uint64_t>(total - sr.logSeen, recent.size());
    for (std::size_t i = recent.size() - static_cast<std::size_t>(fresh); i < recent.size(); ++i) {
        const auto& e = recent[i];
        sr.report << "        log " << e.time.substr(e.time.size() >= 12 ? e.time.size() - 12 : 0) << ' '
                  << minidaw::logLevelName(e.level) << ' ' << e.text << '\n';
    }
    sr.logSeen = total;
    sr.report.flush();
}

// The end of the current line: ok or FAIL, with how long it took and a note.
void scriptFinish(AppState& app, bool ok, const std::string& note)
{
    ScriptRun& sr = *app.script;
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - sr.started).count();
    char head[64];
    std::snprintf(head, sizeof head, "line %3d  %-4s  ", sr.current ? sr.current->number : 0, ok ? "ok" : "FAIL");
    sr.report << head << (sr.current ? sr.current->text : std::string()) << "  (" << std::fixed;
    sr.report.precision(2);
    sr.report << seconds << " s)";
    if (!note.empty()) sr.report << "  " << note;
    sr.report << '\n';
    ++sr.commands;
    if (!ok && sr.stopOnFailure && sr.next < sr.lines.size()) {
        sr.report << "Skipped " << (sr.lines.size() - sr.next) << " line(s) after the failure (stop-on-failure on)\n";
    }
    if (!ok) {
        ++sr.failures;
        app.log.warning("Script line " + std::to_string(sr.current ? sr.current->number : 0) + " failed: " +
                        (sr.current ? sr.current->text : std::string()) + (note.empty() ? "" : " - " + note));
    }
    scriptCopyLog(app);
    sr.pending = ScriptRun::Pending::None;
    sr.current = nullptr;
    if (!ok && sr.stopOnFailure) sr.next = sr.lines.size();  // the rest is skipped
}

// The end of the script (also when mini-daw is closed before it ends).
void scriptEnd(AppState& app, const std::string& why)
{
    ScriptRun& sr = *app.script;
    if (sr.done) return;
    sr.done = true;
    if (!why.empty()) {
        sr.report << "Stopped: " << why << '\n';
        ++sr.failures;
    }
    scriptCopyLog(app);
    sr.report << "Result: " << sr.commands << " command(s), " << sr.failures << " failed\n";
    sr.report.flush();
    app.scriptExitCode = sr.failures == 0 ? 0 : 1;
    app.log.info("Script " + minidaw::pathToUtf8(sr.file) + " done: " + std::to_string(sr.commands) + " command(s), " +
                 std::to_string(sr.failures) + " failed; report " + minidaw::pathToUtf8(sr.reportPath));
    std::printf("script: %d command(s), %d failed; report: %s\n", sr.commands, sr.failures,
                minidaw::pathToUtf8(sr.reportPath).c_str());
    std::fflush(stdout);
    if (!sr.stay && app.mainWindow) glfwSetWindowShouldClose(app.mainWindow, GLFW_TRUE);
}

std::string lowerWord(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

// "3" (or "last": the track the last add-track made) -> the track, or nullptr with `why`.
minidaw::Track* scriptTrack(AppState& app, const std::string& word, std::string& why)
{
    if (lowerWord(word) == "last") {
        minidaw::Track* t = app.script && app.script->lastAdded > 0 ? findTrack(app, app.script->lastAdded) : nullptr;
        if (!t) why = "no track added by the script (last)";
        return t;
    }
    char* end = nullptr;
    const long n = std::strtol(word.c_str(), &end, 10);
    if (word.empty() || *end != '\0' || n <= 0 || n > 100000) {
        why = "\"" + word + "\" is not a track number";
        return nullptr;
    }
    minidaw::Track* t = findTrack(app, static_cast<int>(n));
    if (!t) why = "there is no track " + word;
    return t;
}

// Starts the current line. It either finishes now (scriptFinish) or sets `pending`.
void scriptStart(AppState& app, const minidaw::ScriptLine& line)
{
    ScriptRun& sr = *app.script;
    sr.current = &line;
    sr.started = std::chrono::steady_clock::now();
    const auto& w = line.words;
    const std::string& cmd = w[0];
    std::string why;
    auto fail = [&](const std::string& note) { scriptFinish(app, false, note); };
    auto ok = [&](const std::string& note = {}) { scriptFinish(app, true, note); };

    if (cmd == "echo") {
        std::string text;
        for (std::size_t i = 1; i < w.size(); ++i) text += (i > 1 ? " " : "") + w[i];
        ok(text.empty() ? std::string() : "| " + text);
    } else if (cmd == "wait") {
        const double s = minidaw::parseScriptSeconds(w[1]).value_or(0.0);
        sr.deadline = sr.started + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                       std::chrono::duration<double>(s));
        sr.pending = ScriptRun::Pending::Wait;
    } else if (cmd == "wait-for") {
        const double s = minidaw::parseScriptSeconds(w[1]).value_or(0.0);
        sr.deadline = sr.started + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                       std::chrono::duration<double>(s));
        sr.pending = ScriptRun::Pending::WaitFor;
    } else if (cmd == "expect") {
        const std::string op = w.size() == 4 ? w[2] : "==";
        std::string seen, err;
        const bool holds =
            minidaw::compareStateValue(scriptState(app), w[1], op, w.back(), seen, err, line.quoted.back());
        if (!err.empty()) {
            fail(err);
        } else if (holds) {
            ok();
        } else {
            fail("is " + seen);
        }
    } else if (cmd == "state") {
        ok();
        sr.report << minidaw::stateText(scriptState(app), 2) << '\n';  // never throws on odd names (AA1)
        sr.report.flush();
    } else if (cmd == "stop-on-failure") {
        sr.stopOnFailure = w[1] == "on";
        ok();
    } else if (cmd == "stay") {
        sr.stay = true;
        ok();
    } else if (cmd == "quit") {
        ok();
        sr.next = sr.lines.size();
        sr.stay = false;
    } else if (cmd == "scan") {
        if (app.scanning) {
            fail("a scan is already running");
            return;
        }
        startScan(app);
        sr.pending = ScriptRun::Pending::Scan;
    } else if (cmd == "open") {
        const std::string folder = minidaw::cleanPathText(w[1]);
        sr.busyResult = false;
        sr.lastAdded = 0;  // its tracks are another project's (AA4)
        startBusy(app, "Opening project " + folder, [&app, folder] {
            const bool opened = openProject(app, minidaw::pathFromUtf8(folder));
            if (app.script) app.script->busyResult = opened;
        });
        sr.pending = ScriptRun::Pending::Busy;
    } else if (cmd == "save") {
        if (app.projectFolder.empty()) {
            fail("no project is open");
            return;
        }
        saveProjectNow(app);
        if (app.projectError.empty()) {
            ok();
        } else {
            fail(app.projectError);  // AA3
        }
    } else if (cmd == "add-track") {
        const int n = addTrack(app, w[1] == "midi" ? minidaw::TrackKind::Midi : minidaw::TrackKind::Audio, why);
        if (n == 0) {
            fail(why);
        } else {
            sr.lastAdded = n;
            ok("track " + std::to_string(n));
        }
    } else if (cmd == "remove-track") {
        minidaw::Track* t = scriptTrack(app, w[1], why);
        const int number = t ? t->number : 0;
        if (t && removeTrackNow(app, number, why)) {
            if (number == sr.lastAdded) sr.lastAdded = 0;  // "last" is gone too (AA4)
            ok();
        } else {
            fail(why);
        }
    } else if (cmd == "instrument") {
        minidaw::Track* t = scriptTrack(app, w[1], why);
        if (!t) {
            fail(why);
            return;
        }
        if (t->kind != minidaw::TrackKind::Midi) {
            fail("track " + w[1] + " is not a MIDI track");
            return;
        }
        // By class ID, by name, or by "name (vendor)" as in the list; any case.
        const std::string wanted = lowerWord(w[2]);
        std::vector<minidaw::PluginClass> found;
        for (const auto& c : app.pluginCache.instruments()) {
            if (lowerWord(c.classId) == wanted || lowerWord(instrumentLabel(c)) == wanted) {
                found = {c};
                break;
            }
            if (lowerWord(c.name) == wanted) found.push_back(c);
        }
        if (found.empty()) {
            fail("no instrument \"" + w[2] + "\" found (scan first?)");
        } else if (found.size() > 1) {
            fail("more than one instrument is called \"" + w[2] + "\": use \"name (vendor)\" or the class ID");
        } else {
            sr.busyResult = false;  // set by the work only if this instrument was loaded (AA2)
            if (!requestInstrument(app, t->number, found.front())) {
                fail("other slow work is still to do");
            } else {
                sr.pending = ScriptRun::Pending::Busy;
            }
        }
    } else if (cmd == "output") {
        minidaw::Track* t = scriptTrack(app, w[1], why);
        if (!t) {
            fail(why);
            return;
        }
        if (t->kind != minidaw::TrackKind::Midi) {
            fail("track " + w[1] + " is not a MIDI track");
            return;
        }
        std::string port;
        if (lowerWord(w[2]) != "none") {
            for (const auto& name : app.midiPlayer.outputPorts()) {
                if (name == w[2]) port = name;
            }
            if (port.empty()) {
                fail("no MIDI output \"" + w[2] + "\"");
                return;
            }
        }
        const int number = t->number;
        if (setMidiOutput(app, *t, port)) dropEmptyLinkedTrack(app, number);
        ok();
    } else if (cmd == "arm") {
        minidaw::Track* t = scriptTrack(app, w[1], why);
        if (!t) {
            fail(why);
        } else if (app.session.recording()) {
            fail("not while recording");
        } else if (t->instrumentOf > 0) {
            fail("a linked sound track records with its MIDI track");
        } else {
            setArm(app, *t, w[2] == "on");
            ok();
        }
    } else if (cmd == "edit" || cmd == "close-edit") {
        minidaw::Track* t = scriptTrack(app, w[1], why);
        if (!t) {
            fail(why);
        } else if (cmd == "close-edit") {
            app.pluginWindows.close(t->number);
            ok();
        } else if (app.instruments.count(t->number) == 0) {
            fail("track " + w[1] + " has no instrument loaded");
        } else if (!minidaw::app::PluginWindows::supported()) {
            fail("the instrument's own window is not available on this system");
        } else {
            openPluginWindow(app, *t);
            if (app.pluginWindows.isOpen(t->number)) {
                ok();
            } else {
                fail("not opened (see the log)");
            }
        }
    } else if (cmd == "play" || cmd == "stop") {
        const bool want = cmd == "play";
        if (app.audio.player().playing() != want) togglePlay(app);
        if (app.audio.player().playing() == want) {
            ok();
        } else {
            fail(want ? "did not start (see the log)" : "did not stop (a recording is stopped with record)");
        }
    } else if (cmd == "home") {
        if (app.session.recording() && !app.session.autoMode()) {
            fail("not while recording (as in the window)");  // AA3: returnToStart does nothing then
            return;
        }
        returnToStart(app);
        ok();
    } else if (cmd == "record") {
        const bool was = app.session.recording();
        toggleRecording(app);
        if (app.session.recording() != was) {
            ok(app.session.recording() ? "recording" : "stopped");
        } else {
            fail("nothing changed (see the log)");
        }
    } else {
        fail("not run: unknown command");  // parseScript lets none through
    }
}

// Every frame, after the slow work: the current line goes on, or the next one starts.
// One line per frame, so the window and the state follow each step.
void stepScript(AppState& app)
{
    if (!app.script || app.script->done) return;
    ScriptRun& sr = *app.script;
    if (app.busy) return;  // its message is on screen; it runs next frame
    const auto now = std::chrono::steady_clock::now();
    switch (sr.pending) {
    case ScriptRun::Pending::None:
        break;
    case ScriptRun::Pending::Busy: {
        const auto& w = sr.current->words;
        if (w[0] == "open") {
            if (sr.busyResult) {
                scriptFinish(app, true, "");
            } else {
                scriptFinish(app, false, "not opened (see the log)");
            }
        } else {  // instrument: the work says whether this one was loaded (AA2)
            std::string why;
            const minidaw::Track* t = scriptTrack(app, w[1], why);
            if (sr.busyResult && t) {
                scriptFinish(app, true, "loaded " + t->instrumentName);
            } else {
                scriptFinish(app, false, "not loaded (see the log)");
            }
        }
        return;
    }
    case ScriptRun::Pending::Scan:
        if (app.scanning) return;
        scriptFinish(app, true, std::to_string(app.pluginCache.instruments().size()) + " instrument(s)");
        return;
    case ScriptRun::Pending::Wait:
        if (now < sr.deadline) return;
        scriptFinish(app, true, "");
        return;
    case ScriptRun::Pending::WaitFor: {
        const auto& w = sr.current->words;
        const std::string op = w.size() == 5 ? w[3] : "==";
        std::string seen, err;
        const bool holds = minidaw::compareStateValue(scriptState(app), w[2], op, w.back(), seen, err,
                                                      sr.current->quoted.back());
        if (!err.empty()) {
            scriptFinish(app, false, err);
        } else if (holds) {
            scriptFinish(app, true, "");
        } else if (now >= sr.deadline) {
            scriptFinish(app, false, "still " + seen);
        }
        return;
    }
    }
    if (sr.next == 0 && sr.commands == 0) {  // what happened at start (devices, project, instruments)
        sr.report << "Start:\n";
        scriptCopyLog(app);
    }
    if (sr.next >= sr.lines.size()) {
        scriptEnd(app, "");
        return;
    }
    scriptStart(app, sr.lines[sr.next++]);
}

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
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);  // shown once it has its size and place (SPEC-UI-09)

    const std::string title = std::string(minidaw::kProductName) + " " + minidaw::kVersion;
    GLFWwindow* window = glfwCreateWindow(1280, 760, title.c_str(), nullptr, nullptr);
    app.mainWindow = window;
    if (window == nullptr) {
        const std::string fatal = "The main window could not be created: " + g_lastGlfwError;
        app.log.error(fatal);
        minidaw::app::showFatalError(fatal);
        glfwTerminate();
        return 1;
    }
#if !defined(_WIN32)
    // Window icon (P9b); on Windows it comes from the program's icon resource.
    GLFWimage icons[2] = {
        {minidaw::app::kIcon32Size, minidaw::app::kIcon32Size, const_cast<unsigned char*>(minidaw::app::kIcon32)},
        {minidaw::app::kIcon48Size, minidaw::app::kIcon48Size, const_cast<unsigned char*>(minidaw::app::kIcon48)},
    };
    glfwSetWindowIcon(window, 2, icons);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    minidaw::app::enableStateCues();  // hover / pressed / disabled cues (PL1, SPEC-UI-15)
    ImGui::GetIO().IniFilename = nullptr;  // window layout is not saved
    // Keyboard use (crosscheck #13, U1): Tab / arrows move between controls, Enter
    // presses the one in focus, Escape leaves it. Space and Home stay the transport
    // keys: see claimTransportKeys.
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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

    // The size and place it had when mini-daw closed (SPEC-UI-09), if that is
    // still on a screen. Work areas without the window frame: the saved
    // position is the inside of the window, so its title bar stays on screen.
    const std::optional<minidaw::ScreenRect> restored = [&]() -> std::optional<minidaw::ScreenRect> {
        int left = 0, top = 0, right = 0, bottom = 0;
        glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
        std::vector<minidaw::ScreenRect> areas;
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            minidaw::ScreenRect a;
            glfwGetMonitorWorkarea(monitors[i], &a.x, &a.y, &a.width, &a.height);
            areas.push_back({a.x + left, a.y + top, a.width - left - right, a.height - top - bottom});
        }
        return minidaw::restoredWindowRect(app.config.window, areas);
    }();
    if (restored) {
        glfwSetWindowPos(window, restored->x, restored->y);
        glfwSetWindowSize(window, restored->width, restored->height);
    } else if (app.config.window.remember && app.config.window.hasGeometry) {
        app.log.info("The saved window place is not on any screen now: default place used");
    }

    // Display scaling (crosscheck #8, O7). GLFW makes the process DPI aware, so
    // sizes are real pixels: text and spacing follow the window's content scale
    // (font_size stays the size at 100 %), and the window gets the scaled size,
    // at most 90 % of the screen's work area.
    {
        float xs = 1.0f, ys = 1.0f;
        glfwGetWindowContentScale(window, &xs, &ys);
        const float scale = std::clamp(xs, 1.0f, 4.0f);
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui::GetStyle().FontScaleDpi = scale;
        int wx = 0, wy = 0, ww = 0, wh = 0;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
        int w = static_cast<int>(1280 * scale), h = static_cast<int>(760 * scale);
        if (ww > 0 && wh > 0) {
            w = std::min(w, ww * 9 / 10);
            h = std::min(h, wh * 9 / 10);
        }
        if (restored) {
            w = restored->width;
            h = restored->height;
        } else {
            glfwSetWindowSize(window, w, h);
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "Display scale %.0f %%, window %d x %d%s", scale * 100.0, w, h,
                      restored ? " (as last time)" : "");
        app.log.info(buf);
        // Moved to a monitor with other scaling: the text follows.
        glfwSetWindowContentScaleCallback(window, [](GLFWwindow*, float x, float) {
            ImGui::GetStyle().FontScaleDpi = std::clamp(x, 1.0f, 4.0f);
        });
    }

    // The normal (not maximised, not minimised) place, kept up to date for saving at the end.
    minidaw::ScreenRect normalPlace{};
    const auto noteNormalPlace = [&]() {
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || glfwGetWindowAttrib(window, GLFW_MAXIMIZED)) return;
        glfwGetWindowPos(window, &normalPlace.x, &normalPlace.y);
        glfwGetWindowSize(window, &normalPlace.width, &normalPlace.height);
    };
    if (restored) {
        normalPlace = *restored;
    } else {
        noteNormalPlace();
    }
    // Shown only after the start-up work below (devices, project, instruments),
    // right before the first frame, so no empty window waits for it.
    const bool startMaximized = restored && app.config.window.maximized;

    setContrast(app, app.config.contrast, false);
    setFontSize(app, app.config.fontSize, false);
    app.scan = minidaw::scanDevices();
    logScan(app);
    app.pluginCache = minidaw::loadPluginCache(pluginCacheFile(app));
    checkScanMarker(app);
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

    showMainWindow(window, startMaximized);

    bool quit = false;
    bool quitAfterChooser = false;
    while (!quit) {
        // Closing while a folder is being chosen (crosscheck #14, V2): the chooser is
        // cancelled first (the Windows dialog runs on its own thread and would keep
        // mini-daw alive without a window), then mini-daw closes.
        if (glfwWindowShouldClose(window)) {
            if (!app.folderChooser.active()) break;
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            quitAfterChooser = true;
        }
        if (quitAfterChooser) {
            if (!app.folderChooser.nativeOpen()) break;
            static double lastAsk = -1.0;  // asked again now and then: the dialog may not have been up yet
            if (glfwGetTime() - lastAsk > 0.25) {
                app.folderChooser.cancel();
                lastAsk = glfwGetTime();
            }
        }
        glfwPollEvents();
        runBusy(app);  // slow work asked for in the frame before, now that its message is on screen
        stepScript(app);  // --script (P25a): after the slow work, so a line waiting for it sees the result
        noteNormalPlace();
        app.pluginWindows.update();  // editor windows the user closed
        minidaw::editorRunLoop().poll();  // Linux plugin editors: their X11 events and timers (P24)
        if (const std::size_t dropped = minidaw::editorRunLoop().droppedCount(); dropped != app.runLoopDropped) {
            app.runLoopDropped = dropped;
            app.log.warning("A plugin editor closed a file it had asked mini-daw to watch, without saying so; "
                            "mini-daw stopped watching it.");
        }
        // A second start asked for this window (P20): restore it and bring it to the front.
        static double lastShowCheck = 0.0;
        if (glfwGetTime() - lastShowCheck > 0.5) {
            lastShowCheck = glfwGetTime();
            if (app.instanceLock.showRequested()) {
                if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) glfwRestoreWindow(window);
                glfwShowWindow(window);
                glfwFocusWindow(window);
                glfwRequestWindowAttention(window);  // the taskbar button flashes if the system kept the focus elsewhere
                app.log.info("mini-daw was started again: this window was brought to the front");
            }
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            updateAuto(app);  // auto record goes on while minimised (crosscheck #11, S2)
            glfwWaitEventsTimeout(0.05);  // minimised: don't render
            continue;
        }
        minidaw::app::rendererNewFrame(window);
        ImGui::NewFrame();
        minidaw::app::beginStateCuesFrame();

        // No shortcuts while a modal window (the folder browser) or a menu is open: they
        // would act unseen behind it, e.g. start a recording (crosscheck #14, V4).
        const bool shortcuts = !app.folderChooser.active() &&
                               !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (shortcuts) {
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
            claimTransportKeys();  // in a modal window Space presses the control in focus again
        }
        ensureDefaultTracks(app);
        syncMidiPorts(app);
        updateMeters(app);
        updateTransport(app);
        updateAuto(app);
        storeUnsentNote(app);  // Y3
        updateInstruments(app);
        stepScan(app);
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
            // Tabs (SPEC-UI-11, SPEC-UI-12): in the user's order, draggable; the last one used comes back.
            pollAudioMessages(app);
            if (!app.tabsStarted) {
                app.selectTab = app.config.tabs.last;
                app.tabsStarted = true;
            }
            const std::string barId = "main-tabs-" + std::to_string(app.tabBarGeneration);
            const std::string requested = app.selectTab;
            app.selectTab.clear();
            bool timelineOpen = false;
            if (ImGui::BeginTabBar(barId.c_str(), ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_DrawSelectedOverline)) {
                for (const std::string& id : app.config.tabs.order) {
                    const ImGuiTabItemFlags flags = requested == id ? ImGuiTabItemFlags_SetSelected : 0;
                    if (!ImGui::BeginTabItem(tabLabel(id).c_str(), nullptr, flags)) continue;
                    // A tab being selected shows at the next frame; on a new tab bar's first
                    // frame ImGui shows the first tab meanwhile: not drawn, not saved as the
                    // last one (crosscheck #13, U2).
                    if (!requested.empty() && requested != id) {
                        ImGui::EndTabItem();
                        continue;
                    }
                    if (app.config.tabs.last != id) {
                        app.config.tabs.last = id;
                        saveConfig(app);
                    }
                    if (id == "recording") {
                        drawAudioStatusLine(app);
                        drawTransport(app);
                        drawTracksPanel(app);
                    } else if (id == "timeline") {
                        if (!app.timelineShown) app.recordingsDirty = true;  // read them again when the tab opens
                        timelineOpen = true;
                        drawTransport(app);
                        drawTimeline(app);
                    } else if (id == "audio") {
                        drawAudioStatus(app);
                        ImGui::SeparatorText("Devices");
                        drawDevicesPanel(app);
                    } else if (id == "terms") {
                        drawTermsTab(app);
                    }
                    ImGui::EndTabItem();
                }
                // A tab dragged to another place: keep that order (ImGui keeps it in its tab bar).
                if (ImGuiTabBar* bar = ImGui::GetCurrentTabBar()) {
                    std::vector<std::string> shown;
                    for (ImGuiTabItem& tab : bar->Tabs) {
                        const std::string label = ImGui::TabBarGetTabName(bar, &tab);
                        const std::size_t at = label.find("###tab-");
                        if (at != std::string::npos) shown.push_back(label.substr(at + 7));
                    }
                    if (shown.size() == app.config.tabs.order.size() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        setTabOrder(app, shown);
                    }
                }
                ImGui::EndTabBar();
            }
            app.timelineShown = timelineOpen;
        }
        ImGui::EndChild();
        drawStatusLine(app);
        ImGui::End();

        drawAudioSettingsWindow(app);
        drawRecordingSettingsWindow(app);
        drawCustomContrastWindow(app);
        drawTabsWindow(app);
        drawContrastReportWindow(app);
        drawLogWindow(app);
        drawTakeBrowser(app);
        drawProjectDialog(app);
        drawInstrumentsWindow(app);
        drawSavePresetWindow(app);
        drawRecordingWindows(app);
        updateFolderChooser(app);

        minidaw::app::drawStateCues(app.palette);
        drawBusy(app);
        ImGui::Render();
        const auto& bg = app.palette[minidaw::Role::Background];
        const float clear[4] = {static_cast<float>(bg.r), static_cast<float>(bg.g), static_cast<float>(bg.b), 1.0f};
        minidaw::app::rendererRender(window, clear);
    }

    // Remember the window for the next start (SPEC-UI-09).
    if (app.config.window.remember && normalPlace.width >= 100 && normalPlace.height >= 100) {
        app.config.window.hasGeometry = true;
        app.config.window.x = normalPlace.x;
        app.config.window.y = normalPlace.y;
        app.config.window.width = normalPlace.width;
        app.config.window.height = normalPlace.height;
        app.config.window.maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
        saveConfig(app);
    }
    if (app.session.recording()) app.session.stop();  // finish the files before the stream closes
    stopPlayback(app);
    refreshInstrumentStates(app);
    saveProjectNow(app);
    unloadAllInstruments(app);
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
                "        --record-test <seconds> <folder> | --contrast-report | --version | --help |\n"
                "        --script <file> [--script-report <file>]]\n",
                argv0);
    std::printf("--script runs the window and does what the file says, then quits; exit code 0 if every\n"
                "line worked, 1 if not, 2 for a script with errors (nothing is run), 3 if mini-daw is\n"
                "already running with this config. The report goes to <file>.report.txt. Commands:\n");
    for (const auto& c : minidaw::scriptCommands()) {
        std::printf("  %-42s %s\n", std::string(c.usage).c_str(), std::string(c.text).c_str());
    }
#if defined(_WIN32)
    std::printf("In a terminal, run mini-daw.com (or just `mini-daw`): it prints and returns exit codes;\n"
                "mini-daw.exe is the window program.\n");
#endif
}

#if defined(_WIN32)
// mini-daw.exe has no console (P9b). Given an option that prints, it writes to
// the terminal it was started from, if any (the exit code is then not seen by
// cmd: use mini-daw.com). Only a stream without a handle is reopened, so
// `mini-daw.exe --list-devices > file` still writes stdout to the file and
// errors to the terminal; `--config <file>` alone opens the window without
// touching the terminal (crosscheck #8, O6).
void attachToParentConsole(int argc, char** argv)
{
    bool prints = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0) {
            ++i;  // its value
            continue;
        }
        prints = true;  // every other option prints something (or an error)
    }
    if (!prints) return;
    auto missing = [](DWORD which) {
        const HANDLE h = GetStdHandle(which);
        return h == nullptr || h == INVALID_HANDLE_VALUE;
    };
    const bool noOut = missing(STD_OUTPUT_HANDLE);
    const bool noErr = missing(STD_ERROR_HANDLE);
    if (!noOut && !noErr) return;  // console program, or both redirected
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    if (noOut) freopen_s(&f, "CONOUT$", "w", stdout);
    if (noErr) freopen_s(&f, "CONOUT$", "w", stderr);
}
#endif

} // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    attachToParentConsole(argc, argv);
#endif
    AppState app;
    app.configPath = minidaw::defaultConfigPath();
    enum class Action { Gui, ListDevices, AudioTest, RecordTest, ContrastReport } action = Action::Gui;
    int audioTestSeconds = 3;
    bool audioTestMonitor = false;
    std::filesystem::path recordFolder;
    std::filesystem::path scriptFile;
    std::filesystem::path scriptReport;

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
        } else if (std::strcmp(arg, "--script") == 0 && i + 1 < argc) {
            scriptFile = std::filesystem::path(argv[++i]);  // native encoding, as argv is
        } else if (std::strcmp(arg, "--script-report") == 0 && i + 1 < argc) {
            scriptReport = std::filesystem::path(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", arg);
            return 2;
        }
    }

    if (audioTestMonitor && action != Action::AudioTest) {
        std::fprintf(stderr, "--monitor only works with --audio-test\n");
        return 2;
    }
    // A script is read and checked as a whole before anything starts (P25a, SPEC-RUN-02).
    if (!scriptReport.empty() && scriptFile.empty()) {
        std::fprintf(stderr, "--script-report only works with --script\n");
        return 2;
    }
    if (!scriptFile.empty()) {
        if (action != Action::Gui) {
            std::fprintf(stderr, "--script runs the window: not with --list-devices, --audio-test and the like\n");
            return 2;
        }
        std::ifstream in(scriptFile, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "script %s could not be read\n", minidaw::pathToUtf8(scriptFile).c_str());
            return 2;
        }
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        minidaw::ScriptParse parsed = minidaw::parseScript(text);
        if (scriptReport.empty()) {
            scriptReport = scriptFile;
            scriptReport.replace_extension();
            scriptReport += ".report.txt";
        }
        if (!parsed.errors.empty()) {
            // Also into the report: mini-daw.exe started without a terminal shows no stderr (AA9).
            std::ofstream report(scriptReport, std::ios::binary | std::ios::trunc);
            if (report) report << scriptReportHeader(scriptFile, app.configPath);
            for (const auto& e : parsed.errors) {
                std::fprintf(stderr, "%s: %s\n", minidaw::pathToUtf8(scriptFile).c_str(), e.c_str());
                if (report) report << e << '\n';
            }
            if (report) report << "Not run: the script has " << parsed.errors.size() << " error(s)\n";
            return 2;
        }
        auto run = std::make_unique<ScriptRun>();
        run->file = scriptFile;
        run->lines = std::move(parsed.lines);
        run->reportPath = scriptReport;
        app.script = std::move(run);  // the report is opened once this mini-daw holds the lock (AA9)
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
        app.disabledOpacity = result.disabledOpacity;
        return printContrastReport(app);
    }
    case Action::Gui:
        break;
    }
    // One window per config folder (P20, SPEC-RUN-01); checked before the log is opened,
    // which would otherwise rename the running one's log. Command-line runs are not blocked.
    {
        const std::filesystem::path folder = app.configPath.parent_path().empty()
                                                 ? std::filesystem::current_path()
                                                 : app.configPath.parent_path();
        std::string err;
        switch (app.instanceLock.acquire(folder, err)) {
        case minidaw::InstanceLock::Result::HeldByOther: {
#if defined(_WIN32)
            // The running mini-daw may take the foreground (this process may, it was just started).
            AllowSetForegroundWindow(static_cast<DWORD>(minidaw::InstanceLock::ownerProcess(folder)));
#endif
            if (app.script) {  // a script would act on a window it does not own
                // The report is not touched: it may be the running script's own (AA9).
                std::fprintf(stderr, "mini-daw is already running with config folder %s; the script is not run "
                                     "(use --config with another folder)\n",
                             minidaw::pathToUtf8(folder).c_str());
                return 3;
            }
            minidaw::InstanceLock::requestShow(folder);
            std::fprintf(stderr, "mini-daw is already running (config folder %s); its window is brought to the front.\n",
                         minidaw::pathToUtf8(folder).c_str());
            return 0;
        }
        case minidaw::InstanceLock::Result::Failed:
            app.instanceLockNote = "Not protected against a second start: " + err;
            break;
        case minidaw::InstanceLock::Result::Acquired:
            break;
        }
    }
    if (app.script) {
        ScriptRun& sr = *app.script;
        sr.report.open(sr.reportPath, std::ios::binary | std::ios::trunc);
        if (!sr.report) {
            std::fprintf(stderr, "the report %s could not be written\n", minidaw::pathToUtf8(sr.reportPath).c_str());
            return 2;
        }
        sr.report << scriptReportHeader(sr.file, app.configPath);
        sr.report.flush();
    }
    openLog(app, app.script ? "GUI, script" : "GUI");
    if (!app.instanceLockNote.empty()) app.log.warning(app.instanceLockNote);
    if (app.script) {
        app.log.info("Script " + minidaw::pathToUtf8(app.script->file) + ": " +
                     std::to_string(app.script->lines.size()) + " command(s)");
    }
    const int rc = runGui(app);
    if (app.script) {
        app.script->stay = true;  // the window is gone: nothing to close
        scriptEnd(app, "mini-daw was closed before the end of the script");
        app.log.info("mini-daw closed");
        return rc != 0 ? rc : app.scriptExitCode;
    }
    app.log.info("mini-daw closed");
    return rc;
}
