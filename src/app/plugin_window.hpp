#pragma once

// Plugin editor windows (SPEC-VST-05; P10d): one native window per open
// editor, owned by the main window (stays above it, minimises with it, no
// separate taskbar button). Windows only for now; on other systems
// supported() is false (Linux needs a VST3 run loop for X11 editors, later).
// GUI thread only.

#include "minidaw/instrument.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace minidaw::app {

class PluginWindows {
public:
    PluginWindows();
    ~PluginWindows();
    PluginWindows(const PluginWindows&) = delete;
    PluginWindows& operator=(const PluginWindows&) = delete;

    static bool supported();
    // Opens the editor of `instrument` for `track` (or brings its window to
    // the front). False, with a reason, if the plugin has no usable editor.
    bool open(int track, Vst3Instrument& instrument, const std::string& title, GLFWwindow* owner,
              std::string& error);
    bool isOpen(int track) const;
    void close(int track);
    void closeAll();
    // Once per frame: closes the windows the user closed.
    void update();
    std::vector<int> openTracks() const;

    struct Window;  // internal

private:
    std::map<int, std::unique_ptr<Window>> windows_;
};

} // namespace minidaw::app
