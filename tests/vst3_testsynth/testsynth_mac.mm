// The test synth's editor view on macOS (P34c). Built with ARC.

#include "testsynth_mac.hpp"

#import <Cocoa/Cocoa.h>

// The class name is global in the process: one that no host uses.
@interface MinidawTestSynthView : NSView
@property(nonatomic) testsynth::mac::ClickFn click;
@property(nonatomic) void* context;
@end

@implementation MinidawTestSynthView
- (void)drawRect:(NSRect)dirty
{
    [[NSColor whiteColor] setFill];
    NSRectFill(dirty);
    NSDictionary* attrs = @{NSForegroundColorAttributeName : [NSColor blackColor]};
    [@"minidaw test synth: click left = quieter, right = louder" drawAtPoint:NSMakePoint(8, 8)
                                                               withAttributes:attrs];
}
- (void)mouseDown:(NSEvent*)event
{
    const NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    if (self.click) self.click(self.context, p.x >= NSWidth(self.bounds) / 2);
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    (void)event;
    return YES;
}
@end

namespace testsynth::mac {

void* makeView(void* parent, int width, int height, ClickFn click, void* context)
{
    NSView* host = (__bridge NSView*)parent;
    if (!host) return nullptr;
    MinidawTestSynthView* view = [[MinidawTestSynthView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    view.click = click;
    view.context = context;
    [host addSubview:view];
    return (__bridge_retained void*)view;  // released by removeView
}

void resizeView(void* view, int width, int height)
{
    if (view) [(__bridge NSView*)view setFrameSize:NSMakeSize(width, height)];
}

void removeView(void* view)
{
    if (!view) return;
    NSView* v = (__bridge_transfer NSView*)view;  // released when it goes out of scope
    [v removeFromSuperview];
}

} // namespace testsynth::mac
