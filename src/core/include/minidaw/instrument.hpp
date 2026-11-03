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
#include <memory>
#include <string>

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
    bool load(const std::filesystem::path& module, const std::string& classId, double sampleRate,
              std::string& error);
    void unload();
    bool loaded() const;
    const std::string& name() const;
    const std::string& classId() const;
    const std::filesystem::path& modulePath() const;

    // The plugin's state (processor and controller), as bytes.
    bool getState(std::string& component, std::string& controller, std::string& error) const;
    bool setState(const std::string& component, const std::string& controller, std::string& error);

    // A parameter change from the host (any thread except the audio thread):
    // given to the processor in its next block.
    void setParameter(std::uint32_t id, double normalized);
    // GUI thread, every frame: passes parameter changes from the processor to
    // the controller (so an editor shows them).
    void idle();
    Steinberg::Vst::IEditController* controller() const;

    // Audio thread: processes `frames` frames with these events (sorted by
    // offset); writes the instrument's stereo output to `left` and `right`.
    void process(const InstrumentMidi* events, std::size_t count, std::uint32_t frames,
                 const InstrumentTransport& transport, float* left, float* right);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace minidaw
