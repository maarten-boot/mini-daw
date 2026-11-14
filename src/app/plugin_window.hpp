#pragma once

// Plugin editor windows (SPEC-VST-05; P10d): one native window per open
// editor, owned by the main window (stays above it, minimises with it, no
// separate taskbar button). Windows ("HWND") and, since P24, Linux
// ("X11EmbedWindowID", transient for the main window; the editors' run loop,
// minidaw::editorRunLoop(), is polled by the main loop every frame). On other
// systems, and on Linux without X11, supported() is false. GUI thread only.

#include "minidaw/instrument.hpp"

#include <map>
#include <optional>
#include <utility>
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
    // `place`: the window's top-left corner (screen pixels) if it should go
    // there (P26: where it was at the last close); used only if most of the
    // window would be on a screen, else it goes next to `owner` as usual.
    bool open(int track, Vst3Instrument& instrument, const std::string& title, GLFWwindow* owner,
              std::string& error, std::optional<std::pair<int, int>> place = std::nullopt);
    bool isOpen(int track) const;
    void close(int track);
    void closeAll();
    // Once per frame: closes the windows the user closed.
    void update();
    std::vector<int> openTracks() const;
    // The top-left corner of `track`'s window (to remember it, P26); nothing if not open.
    std::optional<std::pair<int, int>> position(int track) const;

    struct Window;  // internal

private:
    std::map<int, std::unique_ptr<Window>> windows_;
};

} // namespace minidaw::app
