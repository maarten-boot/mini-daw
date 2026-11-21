#pragma once

// Getting started (P32, SPEC-UI-19): the tutorial for beginners. The lessons
// are kept here, apart from the GUI: Help > Getting started shows them as
// steps that tick themselves off, and docs/Getting-started.md is the same
// text as Markdown (tutorialMarkdown(); a test fails when the file differs).

#include <cstddef>
#include <string>
#include <vector>

namespace minidaw {

// What mini-daw can see of a step being done. Steps without one are ticked
// by hand.
enum class TutorialCheck {
    None,
    AudioRunning,      // the audio devices are open and running
    ProjectOpen,       // a project is open
    AudioTrack,        // there is an audio track
    TrackArmed,        // a track is armed
    Recorded,          // a Record take was saved (this session)
    Played,            // Play was used (this session)
    InstrumentsFound,  // the instrument list has at least one instrument
    InstrumentTrack,   // a MIDI track plays an instrument
    EditorOpened,      // an instrument's window was opened (this session)
    MidiTake,          // a MIDI track has a take
    Rendered,          // a Render finished (this session)
    SoundFromChanged,  // "Sound from" was changed (this session)
    Monitoring,        // an audio track has Monitor on
    TwoTakes,          // two tracks or more have a take
    MixChanged,        // volume, mute or solo was changed (this session)
    TimelinePlayed,    // a recording was played from the Timeline (this session)
    AudioExported,     // File > Export audio wrote files (this session)
    DawExported,       // File > Export DAWproject wrote a file (this session)
};

// What the program knows now; the GUI fills it every frame. The "this
// session" ones are set when it happens and stay set.
struct TutorialFacts {
    bool audioRunning = false;
    bool projectOpen = false;
    bool audioTrack = false;
    bool trackArmed = false;
    bool recorded = false;
    bool played = false;
    bool instrumentsFound = false;
    bool instrumentTrack = false;
    bool editorOpened = false;
    bool midiTake = false;
    bool rendered = false;
    bool soundFromChanged = false;
    bool monitoring = false;
    bool twoTakes = false;
    bool mixChanged = false;
    bool timelinePlayed = false;
    bool audioExported = false;
    bool dawExported = false;
};

bool tutorialCheckDone(TutorialCheck check, const TutorialFacts& facts);

struct TutorialStep {
    std::string text;   // what to do, and what you see then
    std::string where;  // the control, as named in mini-daw ("Settings > Audio devices...")
    TutorialCheck check = TutorialCheck::None;
};

struct TutorialLesson {
    std::string title;
    std::string intro;
    std::vector<TutorialStep> steps;
    std::string tip;  // "" if none
};

const std::vector<TutorialLesson>& tutorialLessons();
std::size_t tutorialStepCount();

// The whole tutorial as Markdown: the content of docs/Getting-started.md.
std::string tutorialMarkdown();

} // namespace minidaw
