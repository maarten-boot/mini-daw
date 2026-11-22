// The Cocoa side of the plugin editor windows (P34c). Built with ARC.

#include "plugin_window_mac.hpp"

#import <Cocoa/Cocoa.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace minidaw::app::mac {

void* contentView(GLFWwindow* window)
{
    NSWindow* w = glfwGetCocoaWindow(window);
    return w ? (__bridge void*)[w contentView] : nullptr;
}

void attachToOwner(GLFWwindow* child, GLFWwindow* owner)
{
    NSWindow* c = glfwGetCocoaWindow(child);
    NSWindow* o = glfwGetCocoaWindow(owner);
    if (c && o && [c parentWindow] == nil) [o addChildWindow:c ordered:NSWindowAbove];
}

void detachFromOwner(GLFWwindow* child)
{
    NSWindow* c = glfwGetCocoaWindow(child);
    if (c && [c parentWindow]) [[c parentWindow] removeChildWindow:c];
}

} // namespace minidaw::app::mac
