#include "minidaw/instrument.hpp"

#include "console_capture.hpp"
#include "no_error_dialogs.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"
#include "minidaw/ring_buffer.hpp"
#include "minidaw/run_loop.hpp"

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>

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
    bool fromController = false;  // an edit in the plugin's controller: not echoed back (Q9)
};

} // namespace

// Receives the controller's edits (performEdit) and passes them to the processor.
class ComponentHandlerImpl : public IComponentHandler {
public:
    explicit ComponentHandlerImpl(Vst3Instrument* owner) : owner_(owner) {}
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        owner_->queueParameter(id, value, /*fromController=*/true);
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
    std::string name, classId, vendor;
    std::filesystem::path modulePath;
    double sampleRate = 48000;
    bool active = false;
    std::uint64_t serial = 0;
    bool offline = false;  // loaded for rendering (P11)

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

double Vst3Instrument::sampleRate() const
{
    return impl_->sampleRate;
}

const std::string& Vst3Instrument::vendor() const
{
    return impl_->vendor;
}

std::uint64_t Vst3Instrument::serial() const
{
    return impl_->serial;
}

IEditController* Vst3Instrument::controller() const
{
    return impl_->controller.get();
}

bool Vst3Instrument::load(const std::filesystem::path& module, const std::string& classId, double sampleRate,
                          std::string& error, bool offline)
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
        m.vendor = info->vendor().empty() ? factory.info().vendor() : info->vendor();
        m.classId = classId;
        m.modulePath = module;
        m.sampleRate = sampleRate;
        m.offline = offline;

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

        ProcessSetup setup{offline ? kOffline : kRealtime, kSample32, static_cast<int32>(kMaxBlock), sampleRate};
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
        static std::atomic<std::uint64_t> nextSerial{1};
        m.serial = nextSerial.fetch_add(1);
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
    m.vendor.clear();
    m.classId.clear();
    m.modulePath.clear();
    m.serial = 0;
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

bool Vst3Instrument::setState(const std::string& component, const std::string& controller, std::string& error,
                              std::string* warning)
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
        // The processor's state is what sounds; a controller that refuses its part is
        // reported, not fatal (crosscheck #16, X7).
        cs.seek(0, IBStream::kIBSeekSet, nullptr);
        const tresult rc = m.controller->setComponentState(&cs);
        tresult re = kResultOk;
        if (!controller.empty()) {
            MemoryStream es;
            es.write(const_cast<char*>(controller.data()), static_cast<int32>(controller.size()), &written);
            es.seek(0, IBStream::kIBSeekSet, nullptr);
            re = m.controller->setState(&es);
        }
        const bool refused = (rc != kResultOk && rc != kNotImplemented) || (re != kResultOk && re != kNotImplemented);
        if (refused && warning != nullptr) *warning = "the plugin's controller did not take all of the settings";
    }
    return true;
}

namespace {

// The plugin's program-change parameter: the one on the root unit if there are several.
bool findProgramParameter(IEditController* c, ParameterInfo& found)
{
    if (c == nullptr) return false;
    bool any = false;
    const int32 n = c->getParameterCount();
    for (int32 i = 0; i < n; ++i) {
        ParameterInfo pi{};
        if (c->getParameterInfo(i, pi) != kResultOk || (pi.flags & ParameterInfo::kIsProgramChange) == 0) continue;
        if (!any || pi.unitId == kRootUnitId) found = pi;
        if (pi.unitId == kRootUnitId) return true;
        any = true;
    }
    return any;
}

// The class ID as a .vstpreset stores it (FUID text, as other hosts write it).
std::string presetClassString(const std::string& classId)
{
    const auto uid = VST3::UID::fromString(classId);
    if (!uid) return {};
    char8 text[33] = {};
    FUID::fromTUID(uid->data()).toString(text);
    return std::string(text, 32);
}

} // namespace

bool Vst3Instrument::programs(Programs& out) const
{
    const Impl& m = *impl_;
    out = Programs{};
    IEditController* c = m.controller.get();
    ParameterInfo pi{};
    if (!m.active || !findProgramParameter(c, pi)) return false;
    detail::NoErrorDialogs noDialogs;
    out.parameter = pi.id;
    if (FUnknownPtr<IUnitInfo> units(c); units) {
        // The list of the unit that owns the parameter; another unit's list only when
        // it is the plugin's only one (crosscheck #16, X5).
        ProgramListID list = kNoProgramListId;
        for (int32 i = 0; i < units->getUnitCount(); ++i) {
            UnitInfo u{};
            if (units->getUnitInfo(i, u) == kResultOk && u.id == pi.unitId) list = u.programListId;
        }
        const int32 lists = units->getProgramListCount();
        for (int32 i = 0; i < lists; ++i) {
            ProgramListInfo info{};
            if (units->getProgramListInfo(i, info) != kResultOk) continue;
            if (list == kNoProgramListId ? lists != 1 : info.id != list) continue;
            out.listName = Steinberg::Vst::StringConvert::convert(info.name);
            for (int32 k = 0; k < info.programCount && k < 4096; ++k) {
                String128 name{};
                std::string text;
                if (units->getProgramName(info.id, k, name) == kResultOk) text = Steinberg::Vst::StringConvert::convert(name);
                out.names.push_back(text.empty() ? "Program " + std::to_string(k + 1) : text);
            }
            break;
        }
    }
    // Without a unit's list: the parameter's own value names (a list parameter).
    if (out.names.empty() && pi.stepCount > 0 && pi.stepCount < 4096) {
        for (int32 k = 0; k <= pi.stepCount; ++k) {
            String128 name{};
            std::string text;
            if (c->getParamStringByValue(pi.id, static_cast<double>(k) / pi.stepCount, name) == kResultOk) {
                text = Steinberg::Vst::StringConvert::convert(name);
            }
            out.names.push_back(text.empty() ? "Program " + std::to_string(k + 1) : text);
        }
    }
    // Only what the parameter can reach (its steps, or the list's size for a continuous one).
    out.steps = programSteps(pi.stepCount, out.names.size());
    out.names.resize(reachablePrograms(out.steps, out.names.size()));
    return !out.names.empty();
}

int Vst3Instrument::currentProgram() const
{
    ParameterInfo pi{};
    IEditController* c = impl_->controller.get();
    if (!impl_->active || !findProgramParameter(c, pi)) return -1;
    int steps = pi.stepCount;
    if (steps <= 0) {  // continuous: as many steps as the list has entries
        Programs p;
        if (!programs(p)) return -1;
        steps = p.steps;
    }
    return programIndex(c->getParamNormalized(pi.id), steps);
}

bool Vst3Instrument::selectProgram(int index, std::string& error)
{
    Programs p;
    IEditController* c = impl_->controller.get();
    if (!impl_->active || !programs(p)) {
        error = "The instrument has no program list.";
        return false;
    }
    if (index < 0 || index >= static_cast<int>(p.names.size())) {
        error = "No program " + std::to_string(index + 1) + ".";
        return false;
    }
    const double v = programNormalized(index, p.steps);
    detail::NoErrorDialogs noDialogs;
    c->setParamNormalized(p.parameter, v);   // the controller (and an open editor) follows
    queueParameter(p.parameter, v, false);   // the processor, at its next block
    return true;
}

bool Vst3Instrument::savePreset(const std::filesystem::path& file, std::string& error) const
{
    VstPresetData d;
    d.classId = presetClassString(impl_->classId);
    if (d.classId.empty()) {
        error = "The instrument's class ID is not valid.";
        return false;
    }
    if (!getState(d.component, d.controller, error)) return false;
    return writeFileReplacing(file, encodeVstPreset(d), error);
}

bool Vst3Instrument::loadPreset(const std::filesystem::path& file, std::string& error, std::string* warning)
{
    std::string bytes;
    if (!readWholeFile(file, bytes, error)) return false;
    auto d = decodeVstPreset(bytes, error);
    if (!d) return false;
    FUID wanted, inFile;
    const std::string mine = presetClassString(impl_->classId);
    if (mine.empty() || !wanted.fromString(mine.c_str()) || !inFile.fromString(d->classId.c_str()) || !(wanted == inFile)) {
        error = "the preset is for another plugin";
        return false;
    }
    return setState(d->component, d->controller, error, warning);
}

void Vst3Instrument::setParameter(std::uint32_t id, double normalized)
{
    queueParameter(id, normalized, false);
}

void Vst3Instrument::queueParameter(std::uint32_t id, double normalized, bool fromController)
{
    std::lock_guard<std::mutex> lock(impl_->setParamMutex);  // not the audio thread
    ParamChange c{id, normalized, fromController};
    impl_->toProcessor.write(&c, 1);
}

Vst3Instrument::RestartRequest Vst3Instrument::takeRestartRequest()
{
    RestartRequest r;
    if (!impl_->handler) return r;
    r.flags = impl_->handler->restartFlags.exchange(0);
    r.any = r.flags != 0;
    r.reload = (r.flags & (kReloadComponent | kIoChanged)) != 0;
    r.latencyChanged = (r.flags & kLatencyChanged) != 0;
    return r;
}

std::uint32_t Vst3Instrument::latencySamples() const
{
    return impl_->processor ? impl_->processor->getLatencySamples() : 0;
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
    // `echo`: also tell the controller (so the editor shows a change that did
    // not come from it, e.g. a MIDI controller).
    auto addParam = [&](ParamID id, int32 offset, ParamValue value, bool echo) {
        int32 index = 0;
        // Never more queues than made at load, never more points than reserved.
        bool known = false;
        for (int32 i = 0; i < m.paramsIn.getParameterCount(); ++i) {
            if (m.paramsIn.getParameterData(i)->getParameterId() == id) known = true;
        }
        if (!known && m.paramsIn.getParameterCount() >= kMaxParams) return;
        IParamValueQueue* q = m.paramsIn.addParameterData(id, index);
        if (!q) return;
        if (q->getPointCount() < kMaxPoints) {
            q->addPoint(offset, value, index);
        } else {
            // Full: the latest value replaces the last point's value, at that
            // point's offset (the SDK replaces a point with the same offset:
            // no allocation), so the processor ends at the newest value
            // (crosscheck #9, Q4).
            int32 lastOffset = 0;
            ParamValue lastValue = 0;
            q->getPoint(q->getPointCount() - 1, lastOffset, lastValue);
            q->addPoint(lastOffset, value, index);
        }
        if (echo) {
            ParamChange c{id, value, false};
            m.toController.write(&c, 1);
        }
    };

    std::size_t next = 0;
    for (std::uint32_t done = 0; done < frames;) {
        const std::uint32_t n = std::min(frames - done, kMaxBlock);
        m.events.clear();
        m.paramsIn.clearQueue();
        m.paramsOut.clearQueue();

        ParamChange pc[32];
        const std::size_t got = m.toProcessor.read(pc, 32);
        for (std::size_t i = 0; i < got; ++i) addParam(pc[i].id, 0, pc[i].value, !pc[i].fromController);

        for (; next < count && events[next].offset < done + n; ++next) {
            const InstrumentMidi& e = events[next];
            const int32 offset = static_cast<int32>(e.offset >= done ? e.offset - done : 0);
            const std::uint8_t status = e.data[0] & 0xF0;
            const int16 ch = e.data[0] & 0x0F;
            Event ev{};
            ev.busIndex = 0;
            ev.sampleOffset = offset;
            ev.ppqPosition = 0;
            ev.flags = m.offline ? 0 : Event::kIsLive;  // rendered notes are not played live
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
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, e.data[2] / 127.0, true);
            } else if (status == 0xE0) {
                const auto id = m.midiMap[ch][kPitchBend];
                const int bend = e.data[1] | (e.data[2] << 7);
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, bend / 16383.0, true);
            } else if (status == 0xD0) {
                const auto id = m.midiMap[ch][kAfterTouch];
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, e.data[1] / 127.0, true);
            } else if (status == 0xC0) {
                const auto id = m.midiMap[ch][kCtrlProgramChange];
                if (id != kNoMapping) addParam(static_cast<ParamID>(id), offset, e.data[1] / 127.0, true);
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

// The host side of the view's frame: the view asks here for a new size. On
// Linux it is also the editor's run loop (P24, SPEC-VST-05b): the view asks
// for it from the frame and registers its X11 connection and its timers; they
// run in editorRunLoop(), which the GUI thread polls every frame.
class PlugFrameImpl : public IPlugFrame
#if !defined(_WIN32)
    , public Linux::IRunLoop
#endif
{
public:
    explicit PlugFrameImpl(InstrumentEditor* owner) : owner_(owner) {}
    ~PlugFrameImpl() { dropHandlers(); }
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override;
    tresult PLUGIN_API queryInterface(const TUID queried, void** obj) override
    {
        QUERY_INTERFACE(queried, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(queried, obj, IPlugFrame::iid, IPlugFrame)
#if !defined(_WIN32)
        QUERY_INTERFACE(queried, obj, Linux::IRunLoop::iid, Linux::IRunLoop)
#endif
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1000; }  // owned by the editor
    uint32 PLUGIN_API release() override { return 1000; }

#if !defined(_WIN32)
    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler, Linux::FileDescriptor fd) override
    {
        if (!handler || fd < 0) return kInvalidArgument;
        IPtr<Linux::IEventHandler> keep(handler);  // held while registered, and during each call
        const RunLoop::Id id = editorRunLoop().addReader(fd, [keep, fd] { keep->onFDIsSet(fd); }, this);
        if (id == 0) return kResultFalse;
        handlers_.push_back({handler, false, id});
        return kResultTrue;
    }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override
    {
        return unregister(handler, false);
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler, Linux::TimerInterval milliseconds) override
    {
        if (!handler) return kInvalidArgument;
        IPtr<Linux::ITimerHandler> keep(handler);
        const auto ms = std::chrono::milliseconds(static_cast<std::int64_t>(std::min<Linux::TimerInterval>(milliseconds, 3600000)));
        const RunLoop::Id id = editorRunLoop().addTimer(ms, [keep] { keep->onTimer(); }, this);
        if (id == 0) return kResultFalse;
        handlers_.push_back({handler, true, id});
        return kResultTrue;
    }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override { return unregister(handler, true); }
#endif

    // The editor closed: whatever the view left registered goes too.
    // The list is emptied first: removing a handler can run a plugin
    // destructor that unregisters again (Z2).
    void dropHandlers()
    {
        std::vector<Handler> gone;
        gone.swap(handlers_);
        editorRunLoop().removeOwner(this);
    }

private:
    struct Handler {
        const void* object = nullptr;
        bool timer = false;  // reader or timer: unregisterTimer(p) leaves p's readers (Z5)
        RunLoop::Id id = 0;
    };

    tresult unregister(const void* handler, bool timer)
    {
        // Take the records out first, then remove them from the run loop (Z2).
        std::vector<RunLoop::Id> ids;
        for (auto it = handlers_.begin(); it != handlers_.end();) {
            if (it->object == handler && it->timer == timer) {
                ids.push_back(it->id);
                it = handlers_.erase(it);
            } else {
                ++it;
            }
        }
        for (const RunLoop::Id id : ids) editorRunLoop().remove(id);
        return ids.empty() ? kInvalidArgument : kResultTrue;
    }

    InstrumentEditor* owner_;
    std::vector<Handler> handlers_;
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
    e.frame.dropHandlers();  // after removed(): the view may unregister its own first
    if (e.instrument) e.instrument->impl_->editor = nullptr;
    e.instrument = nullptr;
    e.attached = false;
    e.current = {};
}

} // namespace minidaw
