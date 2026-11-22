// The plugin editor in a real Cocoa view on macOS (P34c, SPEC-TECH-07):
// like T-VST-19 (Windows) and T-VST-39 (X11). The host window is made but
// never shown. Objective-C++ with ARC.

#include "minidaw/instrument.hpp"
#include "minidaw/plugin_scan.hpp"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#import <Cocoa/Cocoa.h>

using namespace minidaw;
using Catch::Approx;

namespace {

std::string testSynthClass()
{
    const ScannedModule m = scanModule(MINIDAW_TEST_VST3);
    REQUIRE(m.classes.size() == 1);
    return m.classes[0].classId;
}

float peak(const std::vector<float>& v, std::size_t from, std::size_t to)
{
    float p = 0;
    for (std::size_t i = from; i < to; ++i) p = std::max(p, std::fabs(v[i]));
    return p;
}

} // namespace

TEST_CASE("T-MAC-07: the plugin editor in an NSView; a click in it reaches the sound", "[vst][mac][T-MAC-07]")
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        Vst3Instrument inst;
        std::string err;
        REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
        NSWindow* host = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 300)
                                                     styleMask:NSWindowStyleMaskTitled
                                                       backing:NSBackingStoreBuffered
                                                         defer:YES];  // never shown
        REQUIRE(host != nil);
        host.releasedWhenClosed = NO;
        NSView* content = host.contentView;
        REQUIRE(content != nil);
        {
            InstrumentEditor ed;
            REQUIRE(ed.create(inst, "NSView", err));
            REQUIRE(ed.attach((__bridge void*)content, err));
            REQUIRE(content.subviews.count == 1);
            NSView* view = content.subviews[0];
            CHECK(NSWidth(view.frame) == 300);
            CHECK(NSHeight(view.frame) == 150);
            CHECK(ed.resize({600, 300}) == InstrumentEditor::Size{600, 300});
            CHECK(NSWidth(view.frame) == 600);
            CHECK(NSHeight(view.frame) == 300);

            // A click on the right half raises the gain from 0.5 to 0.6: in the controller and in the sound.
            const NSPoint at = [view convertPoint:NSMakePoint(500, 100) toView:nil];
            NSEvent* click = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                                location:at
                                           modifierFlags:0
                                               timestamp:0
                                            windowNumber:host.windowNumber
                                                 context:nil
                                             eventNumber:0
                                              clickCount:1
                                                pressure:1.0f];
            REQUIRE(click != nil);
            [view mouseDown:click];
            CHECK(inst.controller()->getParamNormalized(1) == Approx(0.6));
            std::vector<float> l(4800), r(4800);
            const InstrumentMidi on{0, {0x90, 69, 127}, 3};
            inst.process(&on, 1, 4800, {}, l.data(), r.data());
            CHECK(peak(l, 2400, 4800) == Approx(0.6).margin(0.01));
            ed.close();
            CHECK(content.subviews.count == 0);  // the view is gone
        }
        [host close];
    }
}
