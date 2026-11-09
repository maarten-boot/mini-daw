// The plugin editor in a real X11 window (P24, SPEC-VST-05b). Separate from
// test_instrument.cpp: X11's macros (Success, None, ...) clash with other names.
// Skipped without an X11 display (CI); run on Xvfb.

#include "minidaw/instrument.hpp"
#include "minidaw/plugin_scan.hpp"
#include "minidaw/run_loop.hpp"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <X11/Xlib.h>

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

InstrumentMidi msg(std::uint32_t offset, std::uint8_t a, std::uint8_t b, std::uint8_t c)
{
    return InstrumentMidi{offset, {a, b, c}, 3};
}

// The test view's X11 window: the host window's only child.
::Window onlyChild(Display* d, ::Window parent)
{
    ::Window root = 0, up = 0, *kids = nullptr;
    unsigned n = 0;
    ::Window found = 0;
    if (XQueryTree(d, parent, &root, &up, &kids, &n) && n == 1) found = kids[0];
    if (kids) XFree(kids);
    return found;
}
} // namespace

TEST_CASE("T-VST-39: the plugin editor in an X11 window; a click in it reaches the sound", "[vst][T-VST-39]")
{
    Display* d = XOpenDisplay(nullptr);
    if (!d) SKIP("no X11 display (DISPLAY not set)");
    Vst3Instrument inst;
    std::string err;
    REQUIRE(inst.load(MINIDAW_TEST_VST3, testSynthClass(), 48000, err));
    const int screen = DefaultScreen(d);
    const ::Window host = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0, 400, 300, 0, BlackPixel(d, screen),
                                              WhitePixel(d, screen));  // never mapped
    XSync(d, False);
    RunLoop& loop = editorRunLoop();
    {
        InstrumentEditor ed;
        REQUIRE(ed.create(inst, "X11EmbedWindowID", err));
        REQUIRE(ed.attach(reinterpret_cast<void*>(static_cast<std::uintptr_t>(host)), err));
        CHECK(loop.readerCount() == 2);  // the pipe and the view's X11 connection
        const ::Window view = onlyChild(d, host);
        REQUIRE(view != 0);
        XWindowAttributes a;
        XGetWindowAttributes(d, view, &a);
        CHECK(a.width == 300);
        CHECK(a.height == 150);
        CHECK(ed.resize({600, 300}) == InstrumentEditor::Size{600, 300});
        XSync(d, False);
        XGetWindowAttributes(d, view, &a);
        CHECK(a.width == 600);

        // A click on the right half, sent to the view's window: the view reads it from
        // its connection when the run loop calls it, and raises the gain from 0.5 to 0.6.
        XEvent ev{};
        ev.type = ButtonPress;
        ev.xbutton.window = view;
        ev.xbutton.display = d;
        ev.xbutton.button = Button1;
        ev.xbutton.x = 500;
        ev.xbutton.y = 100;
        ev.xbutton.same_screen = True;
        REQUIRE(XSendEvent(d, view, False, ButtonPressMask, &ev) != 0);
        XSync(d, False);
        for (int i = 0; i < 100 && inst.controller()->getParamNormalized(1) < 0.55; ++i) {
            loop.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(inst.controller()->getParamNormalized(1) == Approx(0.6));
        std::vector<float> l(4800), r(4800);
        const InstrumentMidi on = msg(0, 0x90, 69, 127);
        inst.process(&on, 1, 4800, {}, l.data(), r.data());
        CHECK(peak(l, 2400, 4800) == Approx(0.6).margin(0.01));
        ed.close();
        XSync(d, False);
        CHECK(onlyChild(d, host) == 0);  // the view's window is gone
        CHECK(loop.readerCount() == 0);
    }
    XDestroyWindow(d, host);
    XCloseDisplay(d);
}
