#pragma once

// Graphics backend for the GUI (SPEC-TECH-05).
//   Windows:       Direct3D 11, falling back to WARP (Windows' built-in software
//                  renderer) when there is no usable GPU driver, e.g. in a VM.
//   Linux / macOS: OpenGL 3.0.
// GLFW creates the window on every platform, so a native window handle is
// always available (needed later for VST3 plugin editors).

#include <string>

struct GLFWwindow;

namespace minidaw::app {

// Call after glfwInit() and before glfwCreateWindow().
void rendererWindowHints();

// Create the device and initialise the ImGui platform and renderer backends.
// On failure returns false and sets `error` to a message for the user.
bool rendererInit(GLFWwindow* window, std::string& error);

// Human-readable name, e.g. "Direct3D 11 (hardware)".
const std::string& rendererName();

void rendererNewFrame(GLFWwindow* window);
void rendererRender(GLFWwindow* window, const float clearColor[4]);
void rendererShutdown();

// Show a fatal error: a message box on Windows, stderr elsewhere.
void showFatalError(const std::string& message);

} // namespace minidaw::app
