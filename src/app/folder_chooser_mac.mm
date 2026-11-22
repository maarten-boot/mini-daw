// The macOS folder / file dialog (crosscheck #28 CC4). Built with ARC.

#include "folder_chooser_mac.hpp"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace minidaw::app::mac {

namespace {
NSOpenPanel* openPanel = nil;  // the one being shown
} // namespace

bool showOpenPanel(GLFWwindow* owner, const std::string& title, const std::string& startUtf8,
                   const std::string& extension, std::function<void(bool, std::string)> done)
{
    if (openPanel) return false;
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    if (!panel) return false;
    const bool file = !extension.empty();
    panel.canChooseFiles = file;
    panel.canChooseDirectories = !file;
    panel.canCreateDirectories = !file;
    panel.allowsMultipleSelection = NO;
    NSString* text = [NSString stringWithUTF8String:title.c_str()];
    panel.message = text;
    panel.title = text;
    if (file) {
        NSString* ext = [NSString stringWithUTF8String:extension.c_str()];
        if ([ext hasPrefix:@"."]) ext = [ext substringFromIndex:1];
        if (UTType* type = [UTType typeWithFilenameExtension:ext]) panel.allowedContentTypes = @[ type ];
    }
    if (!startUtf8.empty()) {
        panel.directoryURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:startUtf8.c_str()] isDirectory:YES];
    }
    openPanel = panel;
    void (^handler)(NSModalResponse) = ^(NSModalResponse response) {
        std::string path;
        bool chosen = false;
        if (response == NSModalResponseOK && panel.URLs.count > 0) {
            if (const char* fsPath = panel.URLs[0].fileSystemRepresentation) {
                path = fsPath;
                chosen = true;
            }
        }
        openPanel = nil;
        done(chosen, path);
    };
    NSWindow* window = owner ? glfwGetCocoaWindow(owner) : nil;
    if (window) {
        [panel beginSheetModalForWindow:window completionHandler:handler];
    } else {
        [panel beginWithCompletionHandler:handler];
    }
    return true;
}

void cancelOpenPanel()
{
    if (openPanel) [openPanel cancel:nil];
}

} // namespace minidaw::app::mac
