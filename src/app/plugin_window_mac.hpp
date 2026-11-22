#pragma once
// The Cocoa side of the plugin editor windows on macOS (P34c): plain C++
// functions around the few Objective-C calls, so plugin_window.cpp stays C++.

struct GLFWwindow;

namespace minidaw::app::mac {

// The window's content view (an NSView*), which the plugin's view goes into.
void* contentView(GLFWwindow* window);
// Keeps `child` above `owner` and minimises it with it (a child window).
void attachToOwner(GLFWwindow* child, GLFWwindow* owner);
// Before the child window goes.
void detachFromOwner(GLFWwindow* child);

} // namespace minidaw::app::mac
