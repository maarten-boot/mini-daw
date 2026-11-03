#include "minidaw/instrument.hpp"

#include "console_capture.hpp"
#include "no_error_dialogs.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/ring_buffer.hpp"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace minidaw {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// The host object plugins see (IHostApplication: name, attribute lists, messages).
HostApplication& hostContext()
{
    static HostApplication host;
    static std::once_flag once;
    std::call_once(once, [] { PluginContextFactory::instance().setPluginContext(&host); });
    return host;
}

constexpr int32 kMaxEvents = 1024;
constexpr int32 kMaxParams = 64;     // parameter queues made at load; never more per block
constexpr int32 kMaxPoints = 4;      // per queue and block (the SDK reserves 5; more would allocate)
constexpr int kNoMapping = -1;

struct ParamChange {
    ParamID id = 0;
    ParamValue value = 0;
};

} // namespace

// Receives the controller's edits (performEdit) and passes them to the processor.
class ComponentHandlerImpl : public IComponentHandler {
public:
    explicit ComponentHandlerImpl(Vst3Instrument* owner) : owner_(owner) {}
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        owner_->setParameter(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        restartFlags.fetch_or(flags);
        return kResultOk;
    }
    tresult PLUGIN_API queryInterface(const TUID queried, void** obj) override
    {
        QUERY_INTERFACE(queried, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(queried, obj, IComponentHandler::iid, IComponentHandler)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }   // owned by the instrument
    uint32 PLUGIN_API release() override { return 1000; }
    std::atomic<int32> restartFlags{0};

private:
    Vst3Instrument* owner_;
};

struct Vst3Instrument::Impl {
    VST3::Hosting::Module::Ptr module;
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> controller;
    std::unique_ptr<ComponentHandlerImpl> handler;
    std::string name, classId;
    std::filesystem::path modulePath;
    double sampleRate = 48000;
    bool active = false;

    HostProcessData data;
    EventList events{kMaxEvents};
    ParameterChanges paramsIn{kMaxParams};
    ParameterChanges paramsOut{kMaxParams};
    ProcessContext context{};
    // MIDI mapping: [channel][controller 0..130] -> parameter (or kNoMapping).
    std::array<std::array<int64_t, 131>, 16> midiMap{};
    bool hasEventInput = false;

    SpscRing<ParamChange> toProcessor{1024};   // host/editor -> audio thread
    SpscRing<ParamChange> toController{1024};  // audio thread -> GUI thread (idle)
    std::mutex setParamMutex;                  // several host threads may call setParameter
    InstrumentEditor* editor = nullptr;        // the open editor, if any (GUI thread)
};

Vst3Instrument::Vst3Instrument() : impl_(std::make_unique<Impl>()) {}

Vst3Instrument::~Vst3Instrument()
{
    unload();
}

bool Vst3Instrument::loaded() const
{
    return impl_->active;
}

const std::string& Vst3Instrument::name() const
{
    return impl_->name;
}

const std::string& Vst3Instrument::classId() const
{
    return impl_->classId;
}

const std::filesystem::path& Vst3Instrument::modulePath() const
{
    return impl_->modulePath;
}

IEditController* Vst3Instrument::controller() const
{
    return impl_->controller.get();
}

bool Vst3Instrument::load(const std::filesystem::path& module, const std::string& classId, double sampleRate,
                          std::string& error)
{
    unload();
    Impl& m = *impl_;
    hostContext();
    detail::ConsoleCapture capture;
    detail::NoErrorDialogs noDialogs;
    try {
        m.module = VST3::Hosting::Module::create(pathToUtf8(module), error);
        if (!m.module) {
            if (error.empty()) error = "The module could not be loaded.";
            return false;
        }
        auto factory = m.module->getFactory();
        factory.setHostContext(&hostContext());
        const auto infos = factory.classInfos();
        const auto info = std::find_if(infos.begin(), infos.end(), [&](const VST3::Hosting::ClassInfo& c) {
            return c.ID().toString() == classId;
        });
        if (info == infos.end()) {
            error = "The module has no class " + classId + " (was it updated?).";
            unload();
            return false;
        }
        m.provider = owned(new PlugProvider(factory, *info, true));
        if (!m.provider->initialize()) {
            error = "The plugin could not be created.";
            unload();
            return false;
        }
        m.component = m.provider->getComponentPtr();
        m.controller = m.provider->getControllerPtr();
        FUnknownPtr<IAudioProcessor> proc(m.component);
        if (!proc) {
            error = "The plugin is not an audio processor.";
            unload();
            return false;
        }
        m.processor = proc;
        m.name = info->name();
        m.classId = classId;
        m.modulePath = module;
        m.sampleRate = sampleRate;

        if (m.controller) {
            m.handler = std::make_unique<ComponentHandlerImpl>(this);
            m.controller->setComponentHandler(m.handler.get());
        }

        // Busses: the first audio output (stereo if possible) and the first event input.
        if (m.component->getBusCount(kAudio, kOutput) < 1) {
            error = "The plugin has no audio output.";
            unload();
            return false;
        }
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        const int32 nIn = m.component->getBusCount(kAudio, kInput);
        std::vector<SpeakerArrangement> ins(static_cast<std::size_t>(nIn), SpeakerArr::kEmpty);
        m.processor->setBusArrangements(ins.empty() ? nullptr : ins.data(), nIn, &stereo, 1);
        for (int32 i = 0; i < m.component->getBusCount(kAudio, kOutput); ++i) {
            m.component->activateBus(kAudio, kOutput, i, i == 0);
        }
        m.hasEventInput = m.component->getBusCount(kEvent, kInput) > 0;
        if (m.hasEventInput) m.component->activateBus(kEvent, kInput, 0, true);

        ProcessSetup setup{kRealtime, kSample32, static_cast<int32>(kMaxBlock), sampleRate};
        if (m.processor->setupProcessing(setup) != kResultOk) {
            error = "The plugin does not accept " + std::to_string(static_cast<int>(sampleRate)) +
                    " Hz, 32-bit float processing.";
            unload();
            return false;
        }
        if (m.processor->canProcessSampleSize(kSample32) != kResultTrue) {
            error = "The plugin cannot process 32-bit float audio.";
            unload();
            return false;
        }
        m.data.prepare(*m.component, static_cast<int32>(kMaxBlock), kSample32);
        m.data.inputEvents = &m.events;
        m.data.inputParameterChanges = &m.paramsIn;
        m.data.outputParameterChanges = &m.paramsOut;
        m.data.processContext = &m.context;

        // MIDI controller mapping, if the controller offers one.
        for (auto& ch : m.midiMap) ch.fill(kNoMapping);
        if (FUnknownPtr<IMidiMapping> map(m.controller); map) {
            for (int16 ch = 0; ch < 16; ++ch) {
                for (int16 cc = 0; cc <= kCtrlProgramChange; ++cc) {
                    ParamID id = 0;
                    if (map->getMidiControllerAssignment(0, ch, cc, id) == kResultTrue) m.midiMap[ch][cc] = id;
                }
            }
        }

        if (m.component->setActive(true) != kResultOk) {
            error = "The plugin could not be activated.";
            unload();
            return false;
        }
        m.processor->setProcessing(true);  // kNotImplemented is fine
        m.active = true;
        return true;
    } catch (const std::exception& e) {
        error = std::string("The plugin failed while loading: ") + e.what();
        unload();
        return false;
    }
}

void Vst3Instrument::unload()
{
    Impl& m = *impl_;
    if (m.editor) m.editor->close();  // the view uses the controller
    detail::ConsoleCapture capture;
    detail::NoErrorDialogs noDialogs;
    if (m.active) {
        m.processor->setProcessing(false);
        m.component->setActive(false);
        m.active = false;
    }
    if (m.controller) m.controller->setComponentHandler(nullptr);
    m.data.unprepare();
    m.processor = nullptr;
    m.controller = nullptr;
    m.component = nullptr;
    m.provider = nullptr;   // terminates and releases processor and controller
    m.handler.reset();
    m.module = nullptr;     // unloads the library last
    m.name.clear();
    m.classId.clear();
    m.modulePath.clear();
    ParamChange scratch[64];
    while (m.toProcessor.read(scratch, 64) > 0) {
    }
    while (m.toController.read(scratch, 64) > 0) {
    }
}

bool Vst3Instrument::getState(std::string& component, std::string& controller, std::string& error) const
{
    const Impl& m = *impl_;
    if (!m.active) {
        error = "No plugin loaded.";
        return false;
    }
    MemoryStream cs;
    if (m.component->getState(&cs) != kResultOk) {
        error = "The plugin did not give its state.";
        return false;
    }
    component.assign(cs.getData(), static_cast<std::size_t>(cs.getSize()));
    controller.clear();
    if (m.controller) {
        MemoryStream es;
        if (m.controller->getState(&es) == kResultOk) {
            controller.assign(es.getData(), static_cast<std::size_t>(es.getSize()));
        }
    }
    return true;
}

bool Vst3Instrument::setState(const std::string& component, const std::string& controller, std::string& error)
{
    Impl& m = *impl_;
    if (!m.active) {
        error = "No plugin loaded.";
        return false;
    }
    MemoryStream cs;
    int32 written = 0;
    cs.write(const_cast<char*>(component.data()), static_cast<int32>(component.size()), &written);
    cs.seek(0, IBStream::kIBSeekSet, nullptr);
    if (m.component->setState(&cs) != kResultOk) {
        error = "The plugin did not accept the saved state.";
        return false;
    }
    if (m.controller) {
        cs.seek(0, IBStream::kIBSeekSet, nullptr);
        m.controller->setComponentState(&cs);
        if (!controller.empty()) {
            MemoryStream es;
            es.write(const_cast<char*>(controller.data()), static_cast<int32>(controller.size()), &written);
            es.seek(0, IBStream::kIBSeekSet, nullptr);
            m.controller->setState(&es);
        }
    }
    return true;
}

void Vst3Instrument::setParameter(std::uint32_t id, double normalized)
{
    std::lock_guard<std::mutex> lock(impl_->setParamMutex);  // not the audio thread
    ParamChange c{id, normalized};
    impl_->toProcessor.write(&c, 1);
}

void Vst3Instrument::idle()
{
    Impl& m = *impl_;
    ParamChange buf[64];
    std::size_t n = 0;
    while ((n = m.toController.read(buf, 64)) > 0) {
        if (!m.controller) continue;
        for (std::size_t i = 0; i < n; ++i) m.controller->setParamNormalized(buf[i].id, buf[i].value);
    }
}

// Audio thread. No allocation: events and queues were made at load and are capped.
void Vst3Instrument::process(const InstrumentMidi* events, std::size_t count, std::uint32_t frames,
                             const InstrumentTransport& transport, float* left, float* right)
{
    Impl& m = *impl_;
    if (!m.active) {
        std::memset(left, 0, sizeof(float) * frames);
        std::memset(right, 0, sizeof(float) * frames);
        return;
    }
    auto addParam = [&](ParamID id, int32 offset, ParamValue value) {
        int32 index = 0;
        // Never more queues than made at load, never more points than reserved.
        bool known = false;
        for (int32 i = 0; i < m.paramsIn.getParameterCount(); ++i) {
            if (m.paramsIn.getParameterData(i)->getParameterId() == id) known = true;
        }
        if (!known && m.paramsIn.getParameterCount() >= kMaxParams) return;
        IParamValueQueue* q = m.paramsIn.addParameterData(id, index);
        if (q && q->getPointCount() < kMaxPoints) q->addPoint(offset, value, index);
        ParamChange c{id, value};
        m.toController.write(&c, 1);  // so the editor shows it
    };

    std::size_t next = 0;
    for (std::uint32_t done = 0; done < frames;) {
        const std::uint32_t n = std::min(frames - done, kMaxBlock);
        m.events.clear();
        m.paramsIn.clearQueue();
        m.paramsOut.clearQueue();

        ParamChange pc[32];
        const std::size_t got = m.toProcessor.read(pc, 32);
        for (std::size_t i = 0; i < got; ++i) addParam(pc[i].id, 0, pc[i].value);

        for (; next < count && events[next].offset < done + n; ++next) {
            const InstrumentMidi& e = events[next];
            const int32 offset = static_cast<int32>(e.offset >= done ? e.offset - done : 0);
            const std::uint8_t status = e.data[0] & 0xF0;
            const int16 ch = e.data[0] & 0x0F;
            Event ev{};
            ev.busIndex = 0;
            ev.sampleOffset = offset;
            ev.ppqPosition = 0;
            ev.flags = Event::kIsLive;
            if (status == 0x90 && e.data[2] > 0) {
                ev.type = Event::kNoteOnEvent;
                ev.noteOn.channel = ch;
                ev.noteOn.pitch = e.data[1];
                ev.noteOn.velocity = e.data[2] / 127.0f;
                ev.noteOn.noteId = -1;
                if (m.hasEventInput) m.events.addEvent(ev);
            } else if (status == 0x80 || (status == 0x90 && e.data[2] == 0)) {
                ev.type = Event::kNoteOffEvent;
                ev.noteOff.channel = ch;
                ev.noteOff.pitch = e.data[1];
                ev.noteOff.velocity = status == 0x80 ? e.data[2] / 127.0f : 0.0f;
                ev.noteOff.noteId = -1;
                if (m.hasEventInput) m.events.addEvent(ev);
            } else if (status == 0xA0) {
                ev.type = Event::kPolyPressureEvent;
                ev.polyPressure.channel = ch;
                ev.polyPressure.pitch = e.data[1];
                ev.polyPressure.pressure = e.data[2] / 127.0f;
                ev.polyPressure.noteId = -1;
                if (m.hasEventInput) m.events.addEvent(ev);
            } else if (status == 0xB0 && e.data[1] < 128) {
                const auto id = m.midiMap[ch][e.data[1]];
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, e.data[2] / 127.0);
            } else if (status == 0xE0) {
                const auto id = m.midiMap[ch][kPitchBend];
                const int bend = e.data[1] | (e.data[2] << 7);
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, bend / 16383.0);
            } else if (status == 0xD0) {
                const auto id = m.midiMap[ch][kAfterTouch];
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, e.data[1] / 127.0);
            } else if (status == 0xC0) {
                const auto id = m.midiMap[ch][kCtrlProgramChange];
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, e.data[1] / 127.0);
            }
        }

        m.context.state = ProcessContext::kTempoValid | ProcessContext::kTimeSigValid |
                          (transport.playing ? ProcessContext::kPlaying : 0);
        m.context.sampleRate = m.sampleRate;
        m.context.projectTimeSamples = transport.position + done;
        m.context.tempo = 120.0;
        m.context.timeSigNumerator = 4;
        m.context.timeSigDenominator = 4;
        m.data.numSamples = static_cast<int32>(n);

        m.processor->process(m.data);

        if (m.data.numOutputs > 0 && m.data.outputs[0].numChannels > 0) {
            const AudioBusBuffers& out = m.data.outputs[0];
            std::memcpy(left + done, out.channelBuffers32[0], sizeof(float) * n);
            std::memcpy(right + done, out.channelBuffers32[out.numChannels > 1 ? 1 : 0], sizeof(float) * n);
        } else {
            std::memset(left + done, 0, sizeof(float) * n);
            std::memset(right + done, 0, sizeof(float) * n);
        }
        // Changes the processor reports go to the controller.
        for (int32 i = 0; i < m.paramsOut.getParameterCount(); ++i) {
            IParamValueQueue* q = m.paramsOut.getParameterData(i);
            int32 off = 0;
            ParamValue v = 0;
            if (q && q->getPointCount() > 0 && q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) {
                ParamChange c{q->getParameterId(), v};
                m.toController.write(&c, 1);
            }
        }
        done += n;
    }
}

// --- Editor (P10d) ---------------------------------------------------------------

// The host side of the view's frame: the view asks here for a new size.
class PlugFrameImpl : public IPlugFrame {
public:
    explicit PlugFrameImpl(InstrumentEditor* owner) : owner_(owner) {}
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override;
    tresult PLUGIN_API queryInterface(const TUID queried, void** obj) override
    {
        QUERY_INTERFACE(queried, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(queried, obj, IPlugFrame::iid, IPlugFrame)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }  // owned by the editor
    uint32 PLUGIN_API release() override { return 1000; }

private:
    InstrumentEditor* owner_;
};

struct InstrumentEditor::Impl {
    explicit Impl(InstrumentEditor* owner) : frame(owner) {}
    Vst3Instrument* instrument = nullptr;
    IPtr<IPlugView> view;
    PlugFrameImpl frame;
    std::string platform;
    bool attached = false;
    bool inRequest = false;   // inside resizeView: the window is being resized for the view
    Size current;             // the size the view was last given
};

tresult PLUGIN_API PlugFrameImpl::resizeView(IPlugView* view, ViewRect* newSize)
{
    InstrumentEditor::Impl& e = *owner_->impl_;
    if (!newSize || !e.view || view != e.view.get()) return kInvalidArgument;
    const InstrumentEditor::Size wanted{newSize->getWidth(), newSize->getHeight()};
    if (wanted.width <= 0 || wanted.height <= 0) return kInvalidArgument;
    e.inRequest = true;
    const bool ok = owner_->onResizeRequest && owner_->onResizeRequest(wanted);
    e.inRequest = false;
    if (!ok) return kResultFalse;
    // The VST3 rule: after resizing the window for the view, the host calls onSize.
    e.current = wanted;
    ViewRect r = *newSize;
    e.view->onSize(&r);
    return kResultTrue;
}

InstrumentEditor::InstrumentEditor() : impl_(std::make_unique<Impl>(this)) {}

InstrumentEditor::~InstrumentEditor()
{
    close();
}

bool InstrumentEditor::isOpen() const
{
    return impl_->view != nullptr;
}

bool InstrumentEditor::create(Vst3Instrument& instrument, const char* platform, std::string& error)
{
    close();
    Vst3Instrument::Impl& im = *instrument.impl_;
    if (!im.active) {
        error = "No plugin loaded.";
        return false;
    }
    if (!im.controller) {
        error = "The plugin has no controller, so no editor window.";
        return false;
    }
    if (im.editor) {
        error = "The plugin's editor window is already open.";
        return false;
    }
    detail::ConsoleCapture capture;
    IPtr<IPlugView> view = owned(im.controller->createView(ViewType::kEditor));
    if (!view) {
        error = "The plugin has no editor window.";
        return false;
    }
    if (view->isPlatformTypeSupported(platform) != kResultTrue) {
        error = std::string("The plugin's editor window does not support this system (") + platform + ").";
        return false;
    }
    if (view->setFrame(&impl_->frame) != kResultTrue) {
        // Some plugins return kResultOk (= kResultTrue) or nothing useful: not fatal.
    }
    impl_->view = view;
    impl_->instrument = &instrument;
    impl_->platform = platform;
    impl_->attached = false;
    impl_->current = {};
    im.editor = this;
    return true;
}

void InstrumentEditor::setContentScale(float scale)
{
    if (!impl_->view || !(scale > 0.0f)) return;
    FUnknownPtr<IPlugViewContentScaleSupport> support(impl_->view);
    if (support) support->setContentScaleFactor(scale);
}

InstrumentEditor::Size InstrumentEditor::size() const
{
    if (!impl_->view) return {};
    ViewRect r;
    if (impl_->view->getSize(&r) != kResultTrue) return impl_->current;
    return {r.getWidth(), r.getHeight()};
}

bool InstrumentEditor::resizable() const
{
    return impl_->view && impl_->view->canResize() == kResultTrue;
}

bool InstrumentEditor::attach(void* parent, std::string& error)
{
    Impl& e = *impl_;
    if (!e.view) {
        error = "No editor created.";
        return false;
    }
    if (e.attached) return true;
    detail::ConsoleCapture capture;
    if (e.view->attached(parent, e.platform.c_str()) != kResultOk) {
        error = "The plugin could not open its editor in the window.";
        return false;
    }
    e.attached = true;
    e.current = size();
    return true;
}

InstrumentEditor::Size InstrumentEditor::resize(Size wanted)
{
    Impl& e = *impl_;
    if (!e.view || !e.attached) return wanted;
    if (e.inRequest) return wanted;      // resizeView tells the view itself
    if (wanted.width <= 0 || wanted.height <= 0) return e.current;   // minimised
    if (wanted == e.current) return wanted;
    if (!resizable()) return e.current;  // the window should get the view's size back
    ViewRect r(0, 0, wanted.width, wanted.height);
    e.view->checkSizeConstraint(&r);
    const Size got{r.getWidth(), r.getHeight()};
    if (got.width <= 0 || got.height <= 0) return e.current;
    if (got == e.current) return got;
    e.current = got;
    e.view->onSize(&r);
    return got;
}

void InstrumentEditor::close()
{
    Impl& e = *impl_;
    if (e.view) {
        detail::ConsoleCapture capture;
        if (e.attached) e.view->removed();
        e.view->setFrame(nullptr);
        e.view = nullptr;
    }
    if (e.instrument) e.instrument->impl_->editor = nullptr;
    e.instrument = nullptr;
    e.attached = false;
    e.current = {};
}

} // namespace minidaw
