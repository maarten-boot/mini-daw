#pragma once

// A VST3 instrument (SPEC-VST-02, 06; P10b).
//
// Loads one instrument class from a module (via the SDK's PlugProvider, which
// creates the processor and the controller and connects them), sets it up for
// 32-bit float processing at the stream rate and makes it active.
//
// MIDI: note on/off and poly pressure become VST3 note events at their sample
// offset; controllers, pitch bend, channel pressure and program changes become
// parameter changes through the plugin's own MIDI mapping (IMidiMapping), if
// it has one. Parameter changes from the controller (the editor) reach the
// processor through a lock-free ring; changes the processor reports go back
// to the controller the same way (idle(), GUI thread).
//
// Thread rules (VST3): load/unload, state, idle() and the controller on the
// GUI thread; process() on the audio thread only, never while load/unload
// runs (the InstrumentRack guarantees that). process() does not allocate:
// the event list and parameter queues are made at load and capped per block.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Steinberg::Vst {
class IEditController;
}

namespace minidaw {

// One MIDI message for an instrument, at a sample offset in the block.
struct InstrumentMidi {
    std::uint32_t offset = 0;
    std::uint8_t data[3] = {};
    std::uint8_t size = 0;
};

struct InstrumentTransport {
    std::int64_t position = 0;  // timeline frame of the block's first sample
    bool playing = false;
};

class Vst3Instrument {
public:
    static constexpr std::uint32_t kMaxBlock = 4096;  // larger blocks are processed in parts

    Vst3Instrument();
    ~Vst3Instrument();
    Vst3Instrument(const Vst3Instrument&) = delete;
    Vst3Instrument& operator=(const Vst3Instrument&) = delete;

    // GUI thread.
    // `offline`: set up for rendering faster than real time (VST3 kOffline);
    // process() then runs on a worker thread instead of the audio thread.
    bool load(const std::filesystem::path& module, const std::string& classId, double sampleRate,
              std::string& error, bool offline = false);
    void unload();
    bool loaded() const;
    const std::string& name() const;
    const std::string& classId() const;
    const std::filesystem::path& modulePath() const;
    const std::string& vendor() const;  // from the class (or factory) info
    double sampleRate() const;   // the rate it was loaded for
    // Changes with every load (unique in the process); 0 when nothing is loaded.
    std::uint64_t serial() const;

    // The plugin's state (processor and controller), as bytes.
    bool getState(std::string& component, std::string& controller, std::string& error) const;
    bool setState(const std::string& component, const std::string& controller, std::string& error);

    // Programs (PL2, SPEC-VST-07a): the plugin's program list (IUnitInfo), chosen
    // with its program-change parameter. GUI thread. False when it has none.
    struct Programs {
        std::uint32_t parameter = 0;
        std::string listName;
        std::vector<std::string> names;
    };
    bool programs(Programs& out) const;
    int currentProgram() const;  // index in programs().names, -1 if there is no list
    bool selectProgram(int index, std::string& error);

    // .vstpreset files (PL2): the processor and controller state with the class
    // ID. Loading refuses a preset of another plugin. GUI thread.
    bool savePreset(const std::filesystem::path& file, std::string& error) const;
    bool loadPreset(const std::filesystem::path& file, std::string& error);

    // A parameter change from the host (any thread except the audio thread):
    // given to the processor in its next block.
    void setParameter(std::uint32_t id, double normalized);
    // GUI thread, every frame: passes parameter changes from the processor to
    // the controller (so an editor shows them).
    void idle();
    Steinberg::Vst::IEditController* controller() const;

    // GUI thread: what the plugin asked for with restartComponent since the
    // last call (cleared by the call).
    struct RestartRequest {
        bool any = false;
        bool reload = false;          // reload the plugin (its busses changed, or it asked to be reloaded)
        bool latencyChanged = false;  // its latency changed (see latencySamples)
        int flags = 0;                // the raw VST3 flags, for the log
    };
    RestartRequest takeRestartRequest();
    std::uint32_t latencySamples() const;  // the processor's latency (0 if none or nothing loaded)

    // Audio thread: processes `frames` frames with these events (sorted by
    // offset); writes the instrument's stereo output to `left` and `right`.
    void process(const InstrumentMidi* events, std::size_t count, std::uint32_t frames,
                 const InstrumentTransport& transport, float* left, float* right);

private:
    friend class InstrumentEditor;
    friend class ComponentHandlerImpl;
    void queueParameter(std::uint32_t id, double normalized, bool fromController);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The plugin's own editor (SPEC-VST-05; P10d). GUI thread only.
//
// The host makes a native window, asks the editor for its size, attaches the
// editor's view to it and tells the view when the window's client area
// changes size. The view can ask for another size (onResizeRequest); the host
// then resizes its window and calls resize(). Sizes are in pixels on Windows
// and Linux (VST3 rule). An instrument has at most one editor; unloading the
// instrument closes it.
class InstrumentEditor {
public:
    struct Size {
        int width = 0;
        int height = 0;
        bool operator==(const Size&) const = default;
    };

    InstrumentEditor();
    ~InstrumentEditor();
    InstrumentEditor(const InstrumentEditor&) = delete;
    InstrumentEditor& operator=(const InstrumentEditor&) = delete;

    // Creates the plugin's view for `platform` ("HWND" on Windows,
    // "X11EmbedWindowID" on Linux). False, with a reason, if the plugin has no
    // editor, does not support the platform or already has an editor open.
    bool create(Vst3Instrument& instrument, const char* platform, std::string& error);
    // The display scale (1.0 = 100 %), given to plugins that support it.
    // Call before size() and attach(); can be called again later.
    void setContentScale(float scale);
    Size size() const;         // the size the view wants now
    bool resizable() const;    // the user may resize the window
    // Puts the view into `parent`, a native window of the platform.
    bool attach(void* parent, std::string& error);
    // The window's client area has this size now: the view is told (first
    // adjusted to its limits, if it is resizable). Returns the size the view
    // took; if it differs, the host should resize its window to it.
    Size resize(Size wanted);
    // Called when the view asks for a new size; return true when the window
    // was resized to it (then the view is told the new size).
    std::function<bool(Size)> onResizeRequest;
    bool isOpen() const;
    void close();              // removes the view and releases it; safe to repeat

    struct Impl;  // internal

private:
    std::unique_ptr<Impl> impl_;
    friend class PlugFrameImpl;
};

} // namespace minidaw
