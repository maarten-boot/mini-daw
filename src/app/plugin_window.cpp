#include "plugin_window.hpp"
#include "minidaw/config.hpp"  // ScreenRect, restoredWindowRect (before the X11 headers)

#include <cstdint>

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GLFW/glfw3native.h>
#include <windows.h>
#define MINIDAW_PLUGIN_WINDOWS 1
#elif defined(__linux__)
// Last: X11's macros (None, Success, ...) must not meet other headers (P24).
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#define MINIDAW_PLUGIN_WINDOWS 1
#endif

namespace minidaw::app {

struct PluginWindows::Window {
    GLFWwindow* window = nullptr;
    InstrumentEditor editor;
    bool settingSize = false;  // our own glfwSetWindowSize: no size callback loop
};

#if defined(MINIDAW_PLUGIN_WINDOWS)
namespace {

PluginWindows::Window* windowOf(GLFWwindow* w)
{
    return static_cast<PluginWindows::Window*>(glfwGetWindowUserPointer(w));
}

void setClientSize(PluginWindows::Window& pw, InstrumentEditor::Size s)
{
    pw.settingSize = true;
    glfwSetWindowSize(pw.window, s.width, s.height);
    pw.settingSize = false;
}

// The user resized the window: the view decides (its limits); snap back if it differs.
void onWindowSize(GLFWwindow* w, int width, int height)
{
    PluginWindows::Window* pw = windowOf(w);
    if (!pw || pw->settingSize || width <= 0 || height <= 0) return;
    const InstrumentEditor::Size got = pw->editor.resize({width, height});
    if (got.width > 0 && got.height > 0 && !(got == InstrumentEditor::Size{width, height})) setClientSize(*pw, got);
}

// The window moved to a display with another scale: plugins that support
// scaling usually ask for a new size afterwards (resizeView).
void onContentScale(GLFWwindow* w, float xscale, float /*yscale*/)
{
    if (PluginWindows::Window* pw = windowOf(w)) pw->editor.setContentScale(xscale);
}

} // namespace
#endif

PluginWindows::PluginWindows() = default;

PluginWindows::~PluginWindows()
{
    closeAll();
}

bool PluginWindows::supported()
{
#if defined(_WIN32)
    return true;
#elif defined(MINIDAW_PLUGIN_WINDOWS)
    // VST3 editors on Linux need X11 (also XWayland); a GLFW built with
    // Wayland could run without it (Z3).
    return glfwGetPlatform() == GLFW_PLATFORM_X11;
#else
    return false;
#endif
}

bool PluginWindows::open(int track, Vst3Instrument& instrument, const std::string& title, GLFWwindow* owner,
                         std::string& error, std::optional<std::pair<int, int>> place)
{
    if (const auto it = windows_.find(track); it != windows_.end()) {
        glfwShowWindow(it->second->window);
        glfwFocusWindow(it->second->window);
        return true;
    }
#if defined(MINIDAW_PLUGIN_WINDOWS)
    auto pw = std::make_unique<Window>();
    Window& win = *pw;
#if defined(_WIN32)
    const char* platform = "HWND";
#else
    const char* platform = "X11EmbedWindowID";  // the editor gets the run loop from its frame (P24)
#endif
    if (!win.editor.create(instrument, platform, error)) return false;
    float scale = 1.0f, unused = 1.0f;
    if (owner) glfwGetWindowContentScale(owner, &scale, &unused);
    win.editor.setContentScale(scale);
    const InstrumentEditor::Size size = win.editor.size();
    if (size.width <= 0 || size.height <= 0) {
        error = "The plugin's editor window has no size.";
        return false;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // the plugin draws itself
    glfwWindowHint(GLFW_RESIZABLE, win.editor.resizable() ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);  // VST3 sizes are pixels on Windows and Linux
    win.window = glfwCreateWindow(size.width, size.height, title.c_str(), nullptr, nullptr);
    glfwDefaultWindowHints();
    if (!win.window) {
        error = "The editor window could not be created.";
        return false;
    }
#if defined(_WIN32)
    HWND parent = glfwGetWin32Window(win.window);
    if (owner) {
        // Owned by the main window: above it, minimised with it, no taskbar button of its own.
        SetWindowLongPtrW(parent, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(glfwGetWin32Window(owner)));
    }
#else
    // X11: a window the plugin puts its own window into (by ID, through its own
    // connection). Transient for the main window: the window manager keeps it
    // above the main window and minimises it with it.
    Display* display = glfwGetX11Display();
    const ::Window xwindow = glfwGetX11Window(win.window);
    if (!display || xwindow == 0) {
        error = "The instrument's own window needs X11 (or XWayland).";
        win.editor.close();
        glfwDestroyWindow(win.window);
        return false;
    }
    if (owner) XSetTransientForHint(display, xwindow, glfwGetX11Window(owner));
    void* parent = reinterpret_cast<void*>(static_cast<std::uintptr_t>(xwindow));
#endif
    bool placed = false;
    if (place) {  // where it was last time, if that is still (mostly) on a screen (P26)
        std::vector<ScreenRect> areas;
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            ScreenRect a;
            glfwGetMonitorWorkarea(monitors[i], &a.x, &a.y, &a.width, &a.height);
            areas.push_back(a);
        }
        WindowSettings saved;
        saved.hasGeometry = true;
        saved.x = place->first;
        saved.y = place->second;
        saved.width = size.width;
        saved.height = size.height;
        if (const auto r = restoredWindowRect(saved, areas)) {
            glfwSetWindowPos(win.window, r->x, r->y);
            placed = true;
        }
    }
    if (owner && !placed) {
        int ox = 0, oy = 0;
        glfwGetWindowPos(owner, &ox, &oy);
        glfwSetWindowPos(win.window, ox + 60, oy + 80);
    }
#if !defined(_WIN32)
    // The window must exist on the X server before the plugin's own connection uses it.
    XSync(display, False);
#endif
    glfwSetWindowUserPointer(win.window, &win);
    win.editor.onResizeRequest = [&win](InstrumentEditor::Size s) {
        setClientSize(win, s);
        return true;
    };
    if (!win.editor.attach(parent, error)) {
        win.editor.close();
        glfwDestroyWindow(win.window);
        return false;
    }
    // The view may have changed its size while attaching.
    const InstrumentEditor::Size now = win.editor.size();
    if (now.width > 0 && now.height > 0 && !(now == size)) setClientSize(win, now);
    glfwSetWindowSizeCallback(win.window, onWindowSize);
    glfwSetWindowContentScaleCallback(win.window, onContentScale);
    glfwShowWindow(win.window);
    glfwFocusWindow(win.window);
    windows_[track] = std::move(pw);
    return true;
#else
    (void)instrument;
    (void)title;
    (void)owner;
    error = "Plugin editor windows are not available on this system yet (Windows and Linux).";
    return false;
#endif
}

bool PluginWindows::isOpen(int track) const
{
    return windows_.count(track) != 0;
}

void PluginWindows::close(int track)
{
    const auto it = windows_.find(track);
    if (it == windows_.end()) return;
    Window& win = *it->second;
    win.editor.close();  // the view leaves the window before the window goes
    if (win.window) {
        glfwSetWindowUserPointer(win.window, nullptr);
        glfwDestroyWindow(win.window);
    }
    windows_.erase(it);
}

void PluginWindows::closeAll()
{
    while (!windows_.empty()) close(windows_.begin()->first);
}

void PluginWindows::update()
{
    std::vector<int> done;
    for (const auto& [track, win] : windows_) {
        // Closed by the user, or the instrument was unloaded (that closes the editor).
        if (glfwWindowShouldClose(win->window) || !win->editor.isOpen()) done.push_back(track);
    }
    for (const int track : done) close(track);
}

std::vector<int> PluginWindows::openTracks() const
{
    std::vector<int> out;
    for (const auto& [track, win] : windows_) out.push_back(track);
    return out;
}

std::optional<std::pair<int, int>> PluginWindows::position(int track) const
{
    const auto it = windows_.find(track);
    if (it == windows_.end() || !it->second->window) return std::nullopt;
    int x = 0, y = 0;
    glfwGetWindowPos(it->second->window, &x, &y);
    return std::make_pair(x, y);
}

} // namespace minidaw::app
