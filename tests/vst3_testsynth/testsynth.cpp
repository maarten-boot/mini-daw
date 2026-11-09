// mini-daw test synth (P10c): a tiny VST3 instrument for the automated tests.
//
// Processor and controller are separate classes (like most commercial
// plugins), connected by the host. One parameter: Gain (0..1, default 0.5).
// Each note plays a sine at its pitch, amplitude = gain x velocity, until its
// note-off; stereo output, 32-bit float. The processor state is the gain (one
// double), so the tests can check that getState/setState round-trip.
//
// Editor (P10d): resizable (200 x 100 .. 1200 x 800, first 300 x 150). On
// Windows ("HWND") a plain window: the gain as text, a click on the left half
// lowers it, on the right half raises it (a real edit through the host). On
// any system the platform "minidaw-test" attaches without a window and then
// asks the host for 320 x 160, like plugins that adjust their size when they
// open. The scale the host gives is shown in the hidden read-only parameter
// "Editor scale" (normalized = scale / 4), so the tests can check it.
//
// MIDI mapping (IMidiMapping): controller 7 (volume) on any channel → Gain.
// Controller state: the editor's last size (like real plugins keep their
// window state), so the tests can check the controller state round-trip.
//
// Programs (PL2): a program list "Factory" with Soft, Medium and Loud
// (gain 0.2, 0.5, 0.9), published through IUnitInfo (EditControllerEx1) and
// chosen with the program-change parameter "Program" (3); choosing one sets
// the gain, in the processor and in the controller.
//
// Editor on Linux (P24): "X11EmbedWindowID", a plain X11 window like the
// Windows one, drawn and clicked through the host's run loop (IRunLoop from
// the frame): its X11 connection is registered as an event handler. On every
// system where the frame offers IRunLoop the editor also registers a 10 ms
// timer, which writes a byte into a pipe it registered too; the hidden
// read-only parameters "Editor timer ticks" (5) and "Editor reads" (6) count
// the calls (normalized = count / 1000), so the tests can check the run loop.
//
// Test hook: the hidden parameter "Restart request" (4), set by the host,
// makes the controller call restartComponent: above 0.5 with kIoChanged |
// kLatencyChanged, else with kParamValuesChanged.
// Not shipped with mini-daw. Part of mini-daw, MIT licence.

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/common/pluginview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;  // this module's HINSTANCE
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr ParamID kGain = 1;
constexpr ParamID kEditorScale = 2;  // read-only, hidden: the scale the host gave the editor
constexpr ParamID kProgram = 3;         // program change (PL2)
constexpr ParamID kRestartRequest = 4;  // hidden test hook (see above)
constexpr ParamID kEditorTicks = 5;     // read-only, hidden: the editor's timer calls / 1000 (P24)
constexpr ParamID kEditorReads = 6;     // read-only, hidden: the editor's pipe reads / 1000 (P24)
constexpr ProgramListID kFactoryList = 1;
constexpr double kProgramGain[] = {0.2, 0.5, 0.9};  // Soft, Medium, Loud
constexpr int32 kProgramCount = 3;

double programGain(ParamValue normalized)
{
    const int i = std::clamp(static_cast<int>(std::lround(normalized * (kProgramCount - 1))), 0, kProgramCount - 1);
    return kProgramGain[i];
}
constexpr const char* kTestPlatform = "minidaw-test";

// Class IDs (random, fixed): processor and controller.
const FUID kProcessorUID(0x6D696E69, 0x64617774, 0x53796E50, 0x726F6331);
const FUID kControllerUID(0x6D696E69, 0x64617774, 0x53796E43, 0x74726C31);

class TestSynthProcessor : public AudioEffect {
public:
    TestSynthProcessor() { setControllerClass(kControllerUID); }
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new TestSynthProcessor); }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = AudioEffect::initialize(context);
        if (r != kResultOk) return r;
        addEventInput(STR16("MIDI in"), 16);
        addAudioOutput(STR16("Out"), SpeakerArr::kStereo);
        return kResultOk;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        sampleRate_ = setup.sampleRate;
        return AudioEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setActive(TBool state) override
    {
        for (auto& v : voices_) v = {};
        return AudioEffect::setActive(state);
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.inputParameterChanges) {
            for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); ++i) {
                IParamValueQueue* q = data.inputParameterChanges->getParameterData(i);
                ParamValue v = 0;
                int32 offset = 0;
                if (!q || q->getPoint(q->getPointCount() - 1, offset, v) != kResultOk) continue;
                if (q->getParameterId() == kGain) gain_ = v;
                if (q->getParameterId() == kProgram) gain_ = programGain(v);
            }
        }
        if (data.numOutputs < 1 || data.outputs[0].numChannels < 2) return kResultOk;
        float* l = data.outputs[0].channelBuffers32[0];
        float* r = data.outputs[0].channelBuffers32[1];
        IEventList* events = data.inputEvents;
        const int32 nEvents = events ? events->getEventCount() : 0;
        int32 next = 0;
        Event e {};
        for (int32 s = 0; s < data.numSamples; ++s) {
            // Events take effect at their sample offset (sample-accurate).
            while (next < nEvents && events->getEvent(next, e) == kResultOk && e.sampleOffset <= s) {
                if (e.type == Event::kNoteOnEvent && e.noteOn.velocity > 0) {
                    Voice& v = voices_[e.noteOn.pitch & 127];
                    v.on = true;
                    v.amp = e.noteOn.velocity;
                    v.phase = 0;
                    v.step = 2.0 * 3.141592653589793 * 440.0 * std::pow(2.0, (e.noteOn.pitch - 69) / 12.0) / sampleRate_;
                } else if (e.type == Event::kNoteOffEvent || (e.type == Event::kNoteOnEvent && e.noteOn.velocity == 0)) {
                    voices_[(e.type == Event::kNoteOffEvent ? e.noteOff.pitch : e.noteOn.pitch) & 127].on = false;
                }
                ++next;
            }
            double sum = 0;
            for (Voice& v : voices_) {
                if (!v.on) continue;
                sum += std::sin(v.phase) * v.amp;
                v.phase += v.step;
            }
            l[s] = r[s] = static_cast<float>(sum * gain_);
        }
        data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }

    tresult PLUGIN_API setState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        double g = 0;
        if (!s.readDouble(g)) return kResultFalse;
        gain_ = g;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        return s.writeDouble(gain_) ? kResultOk : kResultFalse;
    }

private:
    struct Voice {
        bool on = false;
        double amp = 0, phase = 0, step = 0;
    };
    Voice voices_[128];
    double sampleRate_ = 48000;
    double gain_ = 0.5;
};

struct EditorSize {
    int32 width = 300;
    int32 height = 150;
};

// The run loop interfaces' IDs are defined by the SDK only for Linux (and
// other non-Windows systems); Windows editors have their own message loop.
class TestSynthView : public CPluginView,
                      public IPlugViewContentScaleSupport
#if !defined(_WIN32)
    ,
                      public Linux::IEventHandler,
                      public Linux::ITimerHandler
#endif
{
public:
    TestSynthView(EditController* controller, EditorSize* size)
        : CPluginView(nullptr), controller_(controller), size_(size)
    {
        ViewRect r(0, 0, size->width, size->height);
        setRect(r);
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#if defined(_WIN32)
        if (std::strcmp(type, kPlatformTypeHWND) == 0) return kResultTrue;
#endif
#if defined(__linux__)
        if (std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0) return kResultTrue;
#endif
        return std::strcmp(type, kTestPlatform) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        if (isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
        CPluginView::attached(parent, type);
        startRunLoop();
        if (std::strcmp(type, kTestPlatform) == 0) {
            if (plugFrame) {
                ViewRect wanted(0, 0, 320, 160);
                plugFrame->resizeView(this, &wanted);
            }
            return kResultOk;
        }
#if defined(_WIN32)
        hwnd_ = makeWindow(static_cast<HWND>(parent));
        if (!hwnd_) return kResultFalse;
#endif
#if defined(__linux__)
        if (!makeX11Window(reinterpret_cast<std::uintptr_t>(parent))) {
            stopRunLoop();
            return kResultFalse;
        }
#endif
        return kResultOk;
    }
    tresult PLUGIN_API removed() override
    {
#if defined(_WIN32)
        if (hwnd_) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
#endif
        stopRunLoop();
#if defined(__linux__)
        closeX11Window();
#endif
        return CPluginView::removed();
    }

#if !defined(_WIN32)
    // Linux::IEventHandler: the pipe or the X11 connection can be read.
    void PLUGIN_API onFDIsSet(Linux::FileDescriptor fd) override
    {
        if (fd == pipe_[0]) {
            char buf[64];
            while (::read(pipe_[0], buf, sizeof buf) > 0) {
            }
            ++reads_;
            controller_->setParamNormalized(kEditorReads, std::min(1.0, reads_ / 1000.0));
        }
#if defined(__linux__)
        if (display_ && fd == ConnectionNumber(display_)) handleX11Events();
#endif
        (void)fd;
    }
    // Linux::ITimerHandler: every 10 ms.
    void PLUGIN_API onTimer() override
    {
        ++ticks_;
        controller_->setParamNormalized(kEditorTicks, std::min(1.0, ticks_ / 1000.0));
        if (pipe_[1] >= 0) {
            const char c = 1;
            const ssize_t n = ::write(pipe_[1], &c, 1);
            (void)n;
        }
#if defined(__linux__)
        if (display_ && ticks_ % 20 == 0) drawX11();  // the host may change the gain too
#endif
    }
#endif
    tresult PLUGIN_API onSize(ViewRect* newSize) override
    {
        CPluginView::onSize(newSize);
        if (newSize) {
            size_->width = newSize->getWidth();
            size_->height = newSize->getHeight();
        }
#if defined(_WIN32)
        if (hwnd_ && newSize) MoveWindow(hwnd_, 0, 0, newSize->getWidth(), newSize->getHeight(), TRUE);
#endif
#if defined(__linux__)
        if (display_ && window_ && newSize) {
            XResizeWindow(display_, window_, static_cast<unsigned>(newSize->getWidth()),
                          static_cast<unsigned>(newSize->getHeight()));
            XSync(display_, False);
        }
#endif
        return kResultTrue;
    }
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* r) override
    {
        if (!r) return kInvalidArgument;
        r->right = r->left + std::clamp(r->getWidth(), 200, 1200);
        r->bottom = r->top + std::clamp(r->getHeight(), 100, 800);
        return kResultTrue;
    }
    tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override
    {
        controller_->setParamNormalized(kEditorScale, std::clamp(factor / 4.0, 0.0, 1.0));
        return kResultTrue;
    }

    OBJ_METHODS(TestSynthView, CPluginView)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugViewContentScaleSupport)
#if !defined(_WIN32)
        DEF_INTERFACE(Linux::IEventHandler)
        DEF_INTERFACE(Linux::ITimerHandler)
#endif
    END_DEFINE_INTERFACES(CPluginView)
    REFCOUNT_METHODS(CPluginView)

private:
    // The host's run loop, if its frame offers one (Linux hosts do).
    void startRunLoop()
    {
#if !defined(_WIN32)
        if (!plugFrame) return;
        FUnknownPtr<Linux::IRunLoop> loop(plugFrame);
        if (!loop) return;
        runLoop_ = loop;
        if (::pipe(pipe_) == 0) {
            ::fcntl(pipe_[0], F_SETFL, O_NONBLOCK);
            ::fcntl(pipe_[1], F_SETFL, O_NONBLOCK);
            runLoop_->registerEventHandler(this, pipe_[0]);
        }
        runLoop_->registerTimer(this, 10);
#endif
    }
    void stopRunLoop()
    {
#if !defined(_WIN32)
        if (runLoop_) {
            runLoop_->unregisterTimer(this);
            runLoop_->unregisterEventHandler(this);  // the pipe and the X11 connection
            runLoop_ = nullptr;
        }
        for (int& fd : pipe_) {
            if (fd >= 0) ::close(fd);
            fd = -1;
        }
#endif
    }

    // A click on the left half lowers the gain by 0.1, on the right half raises it.
    void clicked(bool louder)
    {
        const double g = std::clamp(controller_->getParamNormalized(kGain) + (louder ? 0.1 : -0.1), 0.0, 1.0);
        controller_->beginEdit(kGain);
        controller_->setParamNormalized(kGain, g);
        controller_->performEdit(kGain, g);
        controller_->endEdit(kGain);
    }

#if defined(_WIN32)
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<TestSynthView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            SetTextColor(dc, RGB(0, 0, 0));
            SetBkColor(dc, RGB(255, 255, 255));
            wchar_t text[160];
            const int pct = self ? static_cast<int>(std::lround(self->controller_->getParamNormalized(kGain) * 100)) : 0;
            std::swprintf(text, 160, L"mini-daw Test Synth\nGain: %d %%\n\nClick left: quieter\nClick right: louder", pct);
            InflateRect(&rc, -8, -8);
            DrawTextW(dc, text, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (self) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                self->clicked(static_cast<short>(LOWORD(lp)) > (rc.right - rc.left) / 2);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_TIMER:  // the host may change the gain too (automation, state)
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    HWND makeWindow(HWND parent)
    {
        const HINSTANCE inst = reinterpret_cast<HINSTANCE>(&__ImageBase);
        static const wchar_t* kClass = L"MiniDawTestSynthView";
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        if (!GetClassInfoExW(inst, kClass, &wc)) {
            wc = WNDCLASSEXW{};
            wc.cbSize = sizeof wc;
            wc.lpfnWndProc = windowProc;
            wc.hInstance = inst;
            wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
            wc.lpszClassName = kClass;
            if (!RegisterClassExW(&wc)) return nullptr;
        }
        const ViewRect& r = getRect();
        HWND h = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, r.getWidth(), r.getHeight(), parent,
                                 nullptr, inst, nullptr);
        if (!h) return nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetTimer(h, 1, 200, nullptr);
        return h;
    }

    HWND hwnd_ = nullptr;
#endif
#if defined(__linux__)
    bool makeX11Window(std::uintptr_t parent)
    {
        display_ = XOpenDisplay(nullptr);
        if (!display_) return false;
        const ViewRect& r = getRect();
        const int screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(display_, static_cast<::Window>(parent), 0, 0,
                                      static_cast<unsigned>(r.getWidth()), static_cast<unsigned>(r.getHeight()), 0,
                                      BlackPixel(display_, screen), WhitePixel(display_, screen));
        XStoreName(display_, window_, "mini-daw Test Synth view");
        XSelectInput(display_, window_, ExposureMask | ButtonPressMask | StructureNotifyMask);
        XMapWindow(display_, window_);
        XSync(display_, False);  // made on the server before attached() returns (the host may look for it)
        if (runLoop_) runLoop_->registerEventHandler(this, ConnectionNumber(display_));
        return true;
    }
    void closeX11Window()
    {
        if (!display_) return;
        if (window_) XDestroyWindow(display_, window_);
        window_ = 0;
        XCloseDisplay(display_);
        display_ = nullptr;
    }
    void handleX11Events()
    {
        while (display_ && XPending(display_) > 0) {
            XEvent ev;
            XNextEvent(display_, &ev);
            if (ev.xany.window != window_) continue;
            if (ev.type == Expose && ev.xexpose.count == 0) {
                drawX11();
            } else if (ev.type == ButtonPress && ev.xbutton.button == Button1) {
                XWindowAttributes a;
                XGetWindowAttributes(display_, window_, &a);
                clicked(ev.xbutton.x > a.width / 2);
                drawX11();
            }
        }
    }
    void drawX11()
    {
        const int screen = DefaultScreen(display_);
        GC gc = DefaultGC(display_, screen);
        XClearWindow(display_, window_);
        XSetForeground(display_, gc, BlackPixel(display_, screen));
        char gain[64];
        std::snprintf(gain, sizeof gain, "Gain: %d %%",
                      static_cast<int>(std::lround(controller_->getParamNormalized(kGain) * 100)));
        const char* lines[] = {"mini-daw Test Synth", gain, "", "Click left: quieter", "Click right: louder"};
        int y = 20;
        for (const char* line : lines) {
            XDrawString(display_, window_, gc, 8, y, line, static_cast<int>(std::strlen(line)));
            y += 16;
        }
        XFlush(display_);
    }

    Display* display_ = nullptr;
    ::Window window_ = 0;
#endif
#if !defined(_WIN32)
    IPtr<Linux::IRunLoop> runLoop_;
    int pipe_[2] = {-1, -1};
#endif
    int ticks_ = 0;
    int reads_ = 0;
    EditController* controller_;
    EditorSize* size_;  // the controller's (kept in its state)
};

class TestSynthController : public EditControllerEx1, public IMidiMapping {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new TestSynthController); }
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = EditControllerEx1::initialize(context);
        if (r != kResultOk) return r;
        // The program list on the root unit, chosen with the "Program" parameter (PL2).
        auto* list = new ProgramList(STR16("Factory"), kFactoryList, kRootUnitId);
        list->addProgram(STR16("Soft"));
        list->addProgram(STR16("Medium"));
        list->addProgram(STR16("Loud"));
        addUnit(new Unit(STR16("Root"), kRootUnitId, kNoParentUnitId, kFactoryList));
        addProgramList(list);
        Parameter* program = list->getParameter();  // kIsProgramChange | kIsList, 3 steps
        program->getInfo().id = kProgram;
        program->getInfo().defaultNormalizedValue = 0.5;  // Medium (gain 0.5, the default)
        program->setNormalized(0.5);
        parameters.addParameter(program);
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kGain);
        parameters.addParameter(STR16("Editor scale"), nullptr, 0, 0.0,
                                ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, kEditorScale);
        parameters.addParameter(STR16("Restart request"), nullptr, 0, 0.0, ParameterInfo::kIsHidden, kRestartRequest);
        parameters.addParameter(STR16("Editor timer ticks"), nullptr, 0, 0.0,
                                ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, kEditorTicks);
        parameters.addParameter(STR16("Editor reads"), nullptr, 0, 0.0,
                                ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, kEditorReads);
        return kResultOk;
    }
    tresult PLUGIN_API setParamNormalized(ParamID tag, ParamValue value) override
    {
        if (tag == kRestartRequest && value > 0.0 && componentHandler) {
            componentHandler->restartComponent(value > 0.5 ? (kIoChanged | kLatencyChanged) : kParamValuesChanged);
            return kResultOk;
        }
        if (tag == kProgram) EditControllerEx1::setParamNormalized(kGain, programGain(value));
        return EditControllerEx1::setParamNormalized(tag, value);
    }
    IPlugView* PLUGIN_API createView(FIDString name) override
    {
        if (!name || std::strcmp(name, ViewType::kEditor) != 0) return nullptr;
        return new TestSynthView(this, &editorSize_);
    }
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16 /*channel*/, CtrlNumber cc,
                                                   ParamID& id) override
    {
        if (busIndex != 0 || cc != kCtrlVolume) return kResultFalse;
        id = kGain;
        return kResultTrue;
    }
    tresult PLUGIN_API getState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        return s.writeInt32(editorSize_.width) && s.writeInt32(editorSize_.height) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API setState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        int32 w = 0, h = 0;
        if (!s.readInt32(w) || !s.readInt32(h) || w < 200 || h < 100 || w > 1200 || h > 800) return kResultFalse;
        editorSize_.width = w;
        editorSize_.height = h;
        return kResultOk;
    }
    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        double g = 0;
        if (s.readDouble(g)) setParamNormalized(kGain, g);
        return kResultOk;
    }

    OBJ_METHODS(TestSynthController, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(EditControllerEx1)
    REFCOUNT_METHODS(EditControllerEx1)

private:
    EditorSize editorSize_;
};

} // namespace

BEGIN_FACTORY_DEF("mini-daw tests", "https://github.com/maarten-boot/mini-daw", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "mini-daw Test Synth", Vst::kDistributable, "Instrument|Synth", "1.0.0", kVstVersionString,
           TestSynthProcessor::create)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "mini-daw Test Synth Controller", 0, "", "1.0.0", kVstVersionString, TestSynthController::create)

END_FACTORY
