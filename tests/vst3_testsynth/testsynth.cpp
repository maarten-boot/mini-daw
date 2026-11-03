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
// Not shipped with mini-daw. Part of mini-daw, MIT licence.

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/common/pluginview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;  // this module's HINSTANCE
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr ParamID kGain = 1;
constexpr ParamID kEditorScale = 2;  // read-only, hidden: the scale the host gave the editor
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
                if (q && q->getParameterId() == kGain && q->getPoint(q->getPointCount() - 1, offset, v) == kResultOk) {
                    gain_ = v;
                }
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

class TestSynthView : public CPluginView, public IPlugViewContentScaleSupport {
public:
    explicit TestSynthView(EditController* controller) : CPluginView(nullptr), controller_(controller)
    {
        ViewRect r(0, 0, 300, 150);
        setRect(r);
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#if defined(_WIN32)
        if (std::strcmp(type, kPlatformTypeHWND) == 0) return kResultTrue;
#endif
        return std::strcmp(type, kTestPlatform) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void* parent, FIDString type) override
    {
        if (isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
        CPluginView::attached(parent, type);
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
        return kResultOk;
    }
    tresult PLUGIN_API removed() override
    {
#if defined(_WIN32)
        if (hwnd_) DestroyWindow(hwnd_);
        hwnd_ = nullptr;
#endif
        return CPluginView::removed();
    }
    tresult PLUGIN_API onSize(ViewRect* newSize) override
    {
        CPluginView::onSize(newSize);
#if defined(_WIN32)
        if (hwnd_ && newSize) MoveWindow(hwnd_, 0, 0, newSize->getWidth(), newSize->getHeight(), TRUE);
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
    END_DEFINE_INTERFACES(CPluginView)
    REFCOUNT_METHODS(CPluginView)

private:
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
    EditController* controller_;
};

class TestSynthController : public EditController {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new TestSynthController); }
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk) return r;
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kGain);
        parameters.addParameter(STR16("Editor scale"), nullptr, 0, 0.0,
                                ParameterInfo::kIsReadOnly | ParameterInfo::kIsHidden, kEditorScale);
        return kResultOk;
    }
    IPlugView* PLUGIN_API createView(FIDString name) override
    {
        if (!name || std::strcmp(name, ViewType::kEditor) != 0) return nullptr;
        return new TestSynthView(this);
    }
    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        IBStreamer s(state, kLittleEndian);
        double g = 0;
        if (s.readDouble(g)) setParamNormalized(kGain, g);
        return kResultOk;
    }
};

} // namespace

BEGIN_FACTORY_DEF("mini-daw tests", "https://github.com/maarten-boot/mini-daw", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "mini-daw Test Synth", Vst::kDistributable, "Instrument|Synth", "1.0.0", kVstVersionString,
           TestSynthProcessor::create)

DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "mini-daw Test Synth Controller", 0, "", "1.0.0", kVstVersionString, TestSynthController::create)

END_FACTORY
