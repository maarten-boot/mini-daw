#pragma once
// The macOS folder / file dialog (NSOpenPanel) for FolderChooser (crosscheck
// #28 CC4, SPEC-TECH-07). A sheet on the main window: mini-daw keeps drawing,
// and the answer comes on the main thread while GLFW handles events.

#include <functional>
#include <string>

struct GLFWwindow;

namespace minidaw::app::mac {

// Shows the dialog for a folder, or for a file with `extension` (".dawproject").
// `done(chosen, utf8Path)` is called once, later. False if it could not be shown.
bool showOpenPanel(GLFWwindow* owner, const std::string& title, const std::string& startUtf8,
                   const std::string& extension, std::function<void(bool, std::string)> done);
// Closes an open dialog as its Cancel button does.
void cancelOpenPanel();

} // namespace minidaw::app::mac
