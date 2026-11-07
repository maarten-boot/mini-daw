#pragma once

// Choosing a folder with a Browse... button (P19, SPEC-UI-14).
//
// Windows: the system's "Select folder" dialog (IFileOpenDialog), shown from a
// thread of its own so mini-daw's window keeps drawing (meters, auto record)
// while it is open; the main window is its owner, so it stays in front.
// Elsewhere, or if that dialog cannot be made: a folder browser drawn with
// ImGui (a modal window: Up, Home, the folders inside, Choose this folder).

#include <filesystem>
#include <memory>
#include <string>

struct GLFWwindow;

namespace minidaw::app {

class FolderChooser {
public:
    FolderChooser();
    ~FolderChooser();
    FolderChooser(const FolderChooser&) = delete;
    FolderChooser& operator=(const FolderChooser&) = delete;

    // Starts choosing, beginning in `start`. Ignored while one is open.
    void open(GLFWwindow* owner, const std::string& title, const std::filesystem::path& start);
    bool active() const;

    // Every frame, outside other windows: draws the ImGui browser when it is
    // used. Returns true once when a folder was chosen (in `chosen`); false
    // while choosing, after Cancel, or when there is nothing to do.
    bool update(std::filesystem::path& chosen);

    // A problem with the system dialog since the last call (then the ImGui
    // browser is used); empty if none. Given once.
    std::string takeNote();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace minidaw::app
