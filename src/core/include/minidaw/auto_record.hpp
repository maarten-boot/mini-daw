#pragma once

// Auto record (P16, SPEC-REC-08): when parts start and end.
//
// AutoPartTracker is pure logic, driven by the GUI thread with stream frames:
// "now", the frame of the last activity (a MIDI channel message or a loud
// audio block on an armed track), the last activity that starts sound (not a
// note-off or pedal-up: those only keep a part going) and whether notes are
// still held. A part begins at new sound-starting activity, `preroll` before
// it was noticed; it ends `tail` after the last activity once there has been
// none for `pause` and nothing is held (a held note keeps the part open, up
// to `maxHeldQuiet`).

#include <cstdint>
#include <ctime>
#include <string>

namespace minidaw {

struct AutoRecordTiming {
    std::uint64_t pause = 0;         // quiet frames that end a part
    std::uint64_t tail = 0;          // frames kept after the last activity
    std::uint64_t preroll = 0;       // frames kept before the activity was noticed
    std::uint64_t maxHeldQuiet = 0;  // a held note keeps a quiet part open at most this long
};

// Frames for the settings: pause in seconds, 1 s tail and preroll, 60 s held.
AutoRecordTiming autoRecordTiming(int pauseSeconds, unsigned int sampleRate);

class AutoPartTracker {
public:
    enum class Kind { None, Begin, End };
    struct Action {
        Kind kind = Kind::None;
        std::uint64_t frame = 0;  // Begin: first frame of the part; End: the frame after its last
    };

    explicit AutoPartTracker(const AutoRecordTiming& timing = {}) : timing_(timing) {}

    // Auto record switched on: activity up to `lastActivity` is old.
    void reset(std::uint64_t now, std::uint64_t lastActivity);
    static constexpr std::uint64_t kSameAsActivity = UINT64_MAX;
    // `earliestNew`: the earliest sound-starting activity since the last
    // update, if known (MIDI can arrive a little late, several messages at
    // once). `lastStart`: the last sound-starting activity (default: the same
    // as `lastActivity`).
    Action update(std::uint64_t now, std::uint64_t lastActivity, bool held, std::uint64_t earliestNew = UINT64_MAX,
                  std::uint64_t lastStart = kSameAsActivity);
    // The part could not be started (no folder): forget it, and the playing
    // that asked for it; the next playing tries again (crosscheck #11, S4).
    void abort(std::uint64_t lastStart);
    // Auto record switched off: ends a part that is open (at `now`).
    Action finish(std::uint64_t now);

    bool inPart() const { return inPart_; }
    std::uint64_t partStart() const { return start_; }

private:
    AutoRecordTiming timing_;
    std::uint64_t seen_ = 0;      // activity up to here is accounted for
    std::uint64_t lastPoll_ = 0;
    bool inPart_ = false;
    std::uint64_t start_ = 0;
};

// Folder name of a part: "2026-11-06 21-15-03" (no ':' for Windows).
std::string autoPartName(const std::tm& localTime);

} // namespace minidaw
