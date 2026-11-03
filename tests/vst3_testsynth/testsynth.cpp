// mini-daw test synth (P10c): a tiny VST3 instrument for the automated tests.
//
// Processor and controller are separate classes (like most commercial
// plugins), connected by the host. One parameter: Gain (0..1, default 0.5).
// Each note plays a sine at its pitch, amplitude = gain x velocity, until its
// note-off; stereo output, 32-bit float. The processor state is the gain (one
// double), so the tests can check that getState/setState round-trip.
// Not shipped with mini-daw. Part of mini-daw, MIT licence.

#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr ParamID kGain = 1;

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

class TestSynthController : public EditController {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new TestSynthController); }
    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const tresult r = EditController::initialize(context);
        if (r != kResultOk) return r;
        parameters.addParameter(STR16("Gain"), nullptr, 0, 0.5, ParameterInfo::kCanAutomate, kGain);
        return kResultOk;
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
