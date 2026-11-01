// mini-daw — application entry point (step P2 skeleton).
//
// Command line:
//   mini-daw                 open the GUI
//   mini-daw --list-devices  print audio devices and MIDI ports, then exit
//   mini-daw --version       print the version, then exit

#include "minidaw/devices.hpp"
#include "minidaw/version.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>  // also pulls in the system OpenGL 1.1 header (glClear etc.)

#include <cstdio>
#include <cstring>
#include <string>

namespace {

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
    for (const auto& p : scan.midiInputs) {
        std::printf("  [%u] %s\n", p.index, p.name.c_str());
    }
    std::printf("MIDI outputs (%zu):\n", scan.midiOutputs.size());
    for (const auto& p : scan.midiOutputs) {
        std::printf("  [%u] %s\n", p.index, p.name.c_str());
    }
    for (const auto& w : scan.warnings) {
        std::printf("warning: %s\n", w.c_str());
    }
}

// Temporary theme: white background, black text (SPEC-UI-01).
// The full contrast system with presets and config file (SPEC-UI-02..07) is step P3.
void applyBaseTheme()
{
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    style.FrameBorderSize = 1.0f;
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
    for (const auto& p : scan.midiInputs) {
        ImGui::BulletText("[%u] %s", p.index, p.name.c_str());
    }
    if (scan.midiInputs.empty()) ImGui::TextUnformatted("(none)");

    ImGui::SeparatorText("MIDI outputs");
    for (const auto& p : scan.midiOutputs) {
        ImGui::BulletText("[%u] %s", p.index, p.name.c_str());
    }
    if (scan.midiOutputs.empty()) ImGui::TextUnformatted("(none)");

    if (!scan.warnings.empty()) {
        ImGui::SeparatorText("Warnings");
        for (const auto& w : scan.warnings) {
            ImGui::BulletText("%s", w.c_str());
        }
    }
}

int runGui()
{
    glfwSetErrorCallback([](int code, const char* text) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, text);
    });
    if (!glfwInit()) {
        return 1;
    }
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
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini next to the exe; real settings come in P4
    applyBaseTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    minidaw::DeviceScan scan = minidaw::scanDevices();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        drawDevicesPanel(scan);
        ImGui::End();

        ImGui::Render();
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
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

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("%s %s\n", minidaw::kProductName, minidaw::kVersion);
            return 0;
        }
        if (std::strcmp(argv[i], "--list-devices") == 0) {
            printDevices(minidaw::scanDevices());
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("usage: %s [--list-devices | --version | --help]\n", argv[0]);
            return 0;
        }
        std::fprintf(stderr, "unknown option: %s (try --help)\n", argv[i]);
        return 2;
    }
    return runGui();
}
