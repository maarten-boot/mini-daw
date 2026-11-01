// mini-daw — application entry point.
//
// Command line:
//   mini-daw                     open the GUI
//   mini-daw --config <file>     use this config file instead of the default one
//   mini-daw --list-devices      print audio devices and MIDI ports, then exit
//   mini-daw --contrast-report   print every colour pair and its contrast, then exit
//   mini-daw --version           print the version, then exit

#include "imgui_theme.hpp"

#include "minidaw/config.hpp"
#include "minidaw/devices.hpp"
#include "minidaw/theme.hpp"
#include "minidaw/version.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>  // also pulls in the system OpenGL 1.1 header (glClear etc.)

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using minidaw::ContrastPreset;
using minidaw::ContrastSetting;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
struct AppState {
    std::filesystem::path configPath;
    std::vector<std::string> configWarnings;
    std::string saveError;

    ContrastSetting contrast;
    minidaw::Palette palette;                    // after enforcement
    std::vector<minidaw::ColorPair> unresolved;  // should stay empty

    bool showCustomContrast = false;
    bool showContrastReport = false;
    double customInput = 7.0;
    std::string customError;

    minidaw::DeviceScan scan;
};

minidaw::EnforceResult buildPalette(const ContrastSetting& setting)
{
    return minidaw::enforceMinimumContrast(minidaw::defaultPalette(), setting.minimum());
}

// Apply a new contrast setting to the GUI and store it in the config file.
void setContrast(AppState& app, const ContrastSetting& setting, bool save)
{
    app.contrast = setting;
    auto result = buildPalette(setting);
    app.palette = result.palette;
    app.unresolved = result.unresolved;
    minidaw::app::applyPalette(app.palette);
    if (save) {
        app.saveError = minidaw::saveContrastSetting(app.configPath, setting);
    }
}

// Ctrl+Shift+Up / Down: step through AA -> AAA -> Max.
void stepContrast(AppState& app, int direction)
{
    const ContrastSetting steps[] = {{ContrastPreset::AA}, {ContrastPreset::AAA}, {ContrastPreset::Max}};
    const double current = app.contrast.minimum();
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

std::string describeContrast(const ContrastSetting& s)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s (minimum %.4g:1)", s.toString().c_str(), s.minimum());
    return buf;
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
    std::printf("Contrast setting: %s\n", describeContrast(app.contrast).c_str());
    std::printf("%-22s %-22s %-8s %-8s %8s  %s\n", "Foreground", "Background", "FG", "BG", "Ratio", "Result");
    for (const auto& pair : minidaw::colorPairs()) {
        const auto& fg = app.palette[pair.foreground];
        const auto& bg = app.palette[pair.background];
        const double r = minidaw::contrastRatio(fg, bg);
        std::printf("%-22s %-22s %-8s %-8s %7.2f:1  %s\n", std::string(minidaw::roleName(pair.foreground)).c_str(),
                    std::string(minidaw::roleName(pair.background)).c_str(), minidaw::toHex(fg).c_str(),
                    minidaw::toHex(bg).c_str(), r,
                    minidaw::meetsContrast(r, app.contrast.minimum()) ? "pass" : "FAIL");
    }
    return app.unresolved.empty() ? 0 : 1;
}

// ---------------------------------------------------------------------------
// GUI panels
// ---------------------------------------------------------------------------
void drawMenuBar(AppState& app, bool& quit)
{
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", "Alt+F4")) quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::BeginMenu("Contrast")) {
            const auto p = app.contrast.preset;
            if (ImGui::MenuItem("AA  (4.5:1)", nullptr, p == ContrastPreset::AA))
                setContrast(app, {ContrastPreset::AA}, true);
            if (ImGui::MenuItem("AAA (7:1)", nullptr, p == ContrastPreset::AAA))
                setContrast(app, {ContrastPreset::AAA}, true);
            if (ImGui::MenuItem("Max (21:1, black on white)", nullptr, p == ContrastPreset::Max))
                setContrast(app, {ContrastPreset::Max}, true);
            if (ImGui::MenuItem("Custom...", nullptr, p == ContrastPreset::Custom)) {
                app.customInput = app.contrast.minimum();
                app.customError.clear();
                app.showCustomContrast = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Raise contrast", "Ctrl+Shift+Up")) stepContrast(app, +1);
            if (ImGui::MenuItem("Lower contrast", "Ctrl+Shift+Down")) stepContrast(app, -1);
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
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Custom contrast", &app.showCustomContrast, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextWrapped("Minimum contrast ratio for every colour pair, from 4.5 (WCAG AA) to 21 "
                           "(pure black on white).");
        ImGui::SetNextItemWidth(160);
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
    ImGui::SetNextWindowSize(ImVec2(900, 480), ImGuiCond_Appearing);
    if (ImGui::Begin("Contrast report", &app.showContrastReport)) {
        const auto lowest = minidaw::lowestContrast(app.palette);
        ImGui::Text("Setting: %s", describeContrast(app.contrast).c_str());
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
                ImGui::TextUnformatted(minidaw::meetsContrast(r, app.contrast.minimum()) ? "pass" : "FAIL");
            }
            ImGui::EndTable();
        }
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
    ImGui::Text("Contrast: %s, lowest pair %.2f:1. Ctrl+Shift+Up/Down to change.",
                describeContrast(app.contrast).c_str(), lowest.ratio);
    ImGui::Text("Config: %s", app.configPath.string().c_str());
    for (const auto& w : app.configWarnings) ImGui::TextWrapped("Config warning: %s", w.c_str());
    if (!app.saveError.empty()) ImGui::TextWrapped("Could not save config: %s", app.saveError.c_str());
    if (!app.unresolved.empty()) {
        ImGui::Text("Warning: %zu colour pair(s) could not reach the minimum.", app.unresolved.size());
    }
}

int runGui(AppState& app)
{
    glfwSetErrorCallback([](int code, const char* text) { std::fprintf(stderr, "GLFW error %d: %s\n", code, text); });
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    const std::string title = std::string(minidaw::kProductName) + " " + minidaw::kVersion;
    GLFWwindow* window = glfwCreateWindow(1100, 700, title.c_str(), nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // window layout is not saved
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    setContrast(app, app.contrast, false);
    app.scan = minidaw::scanDevices();

    bool quit = false;
    while (!quit && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_UpArrow)) stepContrast(app, +1);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_DownArrow)) stepContrast(app, -1);

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);
        drawMenuBar(app, quit);
        const float statusHeight = ImGui::GetTextLineHeightWithSpacing() * 3.5f;
        if (ImGui::BeginChild("content", ImVec2(0, -statusHeight))) {
            drawDevicesPanel(app.scan);
        }
        ImGui::EndChild();
        drawStatusLine(app);
        ImGui::End();

        drawCustomContrastWindow(app);
        drawContrastReportWindow(app);

        ImGui::Render();
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        const auto& bg = app.palette[minidaw::Role::Background];
        glClearColor(static_cast<float>(bg.r), static_cast<float>(bg.g), static_cast<float>(bg.b), 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void printUsage(const char* argv0)
{
    std::printf("usage: %s [--config <file>] [--list-devices | --contrast-report | --version | --help]\n", argv0);
}

} // namespace

int main(int argc, char** argv)
{
    AppState app;
    app.configPath = minidaw::defaultConfigPath();
    enum class Action { Gui, ListDevices, ContrastReport } action = Action::Gui;

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
        } else if (std::strcmp(arg, "--config") == 0 && i + 1 < argc) {
            app.configPath = argv[++i];
        } else {
            std::fprintf(stderr, "unknown option: %s (try --help)\n", arg);
            return 2;
        }
    }

    auto loaded = minidaw::loadConfigFile(app.configPath);
    app.contrast = loaded.config.contrast;
    app.configWarnings = loaded.warnings;
    for (const auto& w : app.configWarnings) std::fprintf(stderr, "config warning: %s\n", w.c_str());

    switch (action) {
    case Action::ListDevices:
        printDevices(minidaw::scanDevices());
        return 0;
    case Action::ContrastReport: {
        auto result = buildPalette(app.contrast);
        app.palette = result.palette;
        app.unresolved = result.unresolved;
        return printContrastReport(app);
    }
    case Action::Gui:
        break;
    }
    return runGui(app);
}
