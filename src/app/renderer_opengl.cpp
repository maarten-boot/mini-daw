// OpenGL 3.0 renderer (Linux, macOS).

#include "renderer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>  // also pulls in the system OpenGL 1.1 header (glClear etc.)

#include <cstdio>

namespace minidaw::app {
namespace {
std::string g_name = "OpenGL";
}

void rendererWindowHints()
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
}

bool rendererInit(GLFWwindow* window, std::string& error)
{
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        error = "OpenGL 3.0 could not be initialised.";
        return false;
    }
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    g_name = std::string("OpenGL ") + (version ? version : "?");
    return true;
}

const std::string& rendererName()
{
    return g_name;
}

void rendererNewFrame(GLFWwindow* /*window*/)
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
}

void rendererRender(GLFWwindow* window, const float clearColor[4])
{
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

void rendererShutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
}

void showFatalError(const std::string& message)
{
    std::fprintf(stderr, "error: %s\n", message.c_str());
}

} // namespace minidaw::app
