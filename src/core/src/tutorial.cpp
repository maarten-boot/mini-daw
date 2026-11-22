#include "minidaw/tutorial.hpp"

namespace minidaw {

bool tutorialCheckDone(TutorialCheck check, const TutorialFacts& f)
{
    switch (check) {
    case TutorialCheck::None: return false;
    case TutorialCheck::AudioRunning: return f.audioRunning;
    case TutorialCheck::ProjectOpen: return f.projectOpen;
    case TutorialCheck::AudioTrack: return f.audioTrack;
    case TutorialCheck::TrackArmed: return f.trackArmed;
    case TutorialCheck::Recorded: return f.recorded;
    case TutorialCheck::Played: return f.played;
    case TutorialCheck::InstrumentsFound: return f.instrumentsFound;
    case TutorialCheck::InstrumentTrack: return f.instrumentTrack;
    case TutorialCheck::EditorOpened: return f.editorOpened;
    case TutorialCheck::MidiTake: return f.midiTake;
    case TutorialCheck::Rendered: return f.rendered;
    case TutorialCheck::SoundFromChanged: return f.soundFromChanged;
    case TutorialCheck::Monitoring: return f.monitoring;
    case TutorialCheck::TwoTakes: return f.twoTakes;
    case TutorialCheck::MixChanged: return f.mixChanged;
    case TutorialCheck::TimelinePlayed: return f.timelinePlayed;
    case TutorialCheck::AudioExported: return f.audioExported;
    case TutorialCheck::DawExported: return f.dawExported;
    }
    return false;
}

namespace {

using C = TutorialCheck;

std::vector<TutorialLesson> makeLessons()
{
    return {
        {"1. Set up and record your first take",
         "Choose where the sound goes in and out, make a project, and record yourself on one track. Use "
         "headphones: they keep the speakers out of the recording.",
         {
             {"Choose your output (speakers or headphones) and your input (microphone, line in or audio "
              "interface), then Apply. The Output meter moves when something plays.",
              "Settings > Audio devices...", C::AudioRunning},
             {"Make a project: give it a name, mini-daw makes a folder for it. Everything you record goes there, "
              "and it is saved by itself.",
              "File > New project...", C::ProjectOpen},
             {"You need an audio track: at the first start there is one already, or add one. Choose its input in "
              "the Input column (the channel your microphone or instrument is on).",
              "Recording tab > Add audio track, Input", C::AudioTrack},
             {"Tick Arm on the track: it will be recorded. Sing or play: its Level meter moves. Keep it out of "
              "the red (CLIP).",
              "Recording tab > the track's Arm", C::TrackArmed},
             {"Press Record, play or sing a few seconds, then press Stop recording. The take is saved in the "
              "project's folder; the track's Take column names it.",
              "Record (Ctrl+R), then Stop recording (Ctrl+R)", C::Recorded},
             {"Go back to the start and press Play: you hear what you recorded.",
              "|< Start (Home), then Play (Space)", C::Played},
         },
         "No sound in? Check Settings > Audio devices... and the track's Input. The line under the transport says "
         "what mini-daw is doing; View > Log (Ctrl+L) has the details."},

        {"2. MIDI and an instrument",
         "A MIDI keyboard plays a VST3 instrument (a software synth or piano). mini-daw records what you play "
         "(the notes) and can turn it into sound.",
         {
             {"Let mini-daw find your instruments: Scan. They are listed with their maker. (Instruments in "
              "another folder: Add folder, then Scan.)",
              "Settings > Instruments (VST\xc2\xae" "3)... > Scan", C::InstrumentsFound},
             {"Add a MIDI track, choose your keyboard as its Input and an instrument in its Output column. A "
              "track \"... sound\" appears below it: there the instrument's sound will be recorded.",
              "Recording tab > Add MIDI track", C::InstrumentTrack},
             {"Press Edit: the instrument's own window opens. Choose a sound (preset) there and play: you hear "
              "it at once.",
              "The MIDI track's Output column > Edit", C::EditorOpened},
             {"Arm the MIDI track (and disarm the audio track), Record, play a few bars, Stop recording. The MIDI "
              "track's take holds your notes, the \"... sound\" track the instrument's sound.",
              "Arm, then Record (Ctrl+R)", C::MidiTake},
             {"Render: the instrument plays the MIDI take again, faster than real time, and its sound becomes "
              "the \"... sound\" track's take. Use it after you chose another sound.",
              "The MIDI track's Output column > Render", C::Rendered},
             {"Choose what plays: \"Sound from: instrument\" (the instrument plays your notes) or \"Sound from: "
              "this recording\" (the recorded sound). Only one plays, so nothing sounds twice.",
              "The \"... sound\" track's Input column", C::SoundFromChanged},
         },
         "No instrument found? Most install into C:\\Program Files\\Common Files\\VST3 (Windows), ~/.vst3 "
         "(Linux) or /Library/Audio/Plug-Ins/VST3 (macOS). An instrument that fails or crashes is listed with the "
         "reason and skipped."},

        {"3. Overdub and mix",
         "Record a second part while the first one plays, then set the levels.",
         {
             {"Add another audio track (or use the first one again), tick Monitor to hear yourself, and Arm "
              "only this track.",
              "Recording tab > Add audio track, Monitor, Arm", C::Monitoring},
             {"Go back to the start and Record: the other tracks play while you record, so you can play along. "
              "Stop recording when done.",
              "|< Start (Home), then Record (Ctrl+R)", C::TwoTakes},
             {"Play it all, and set the mix: Volume, M (mute) and S (solo) per track. Volume, mute and solo "
              "count when you export a mix.",
              "Recording tab > Volume, mute, solo", C::MixChanged},
             {"Every Record take (R) and Auto record part (A) is in the Timeline, in the order you made them. "
              "Click one and Play it, or give it a name and notes.",
              "Timeline tab > a recording > Play it", C::TimelinePlayed},
         },
         "Auto record (next to Record) waits for you to play and records each time you do: handy for practising. "
         "Its parts are in the Timeline too."},

        {"4. Save and share",
         "Your project saves itself. To give your music to others, export it.",
         {
             {"Open a project again later from the File menu; the last one opens when mini-daw starts.",
              "File > Open project... / Recent projects", C::None},
             {"Recordings are made smaller after they end: the WAV file becomes FLAC, the same sound in about "
              "half the space. On by default; older recordings with Compact recordings...",
              "Settings > Recording..., File > Compact recordings...", C::None},
             {"Export audio: the song (as Play plays it) or recordings from the Timeline, as one mix or a file "
              "per track (stems), as Ogg Vorbis, Opus, FLAC or WAV, optionally in a zip with the MIDI files.",
              "File > Export audio...", C::AudioExported},
             {"Export DAWproject: the whole project, to open in another music program (e.g. Bitwig Studio, "
              "Cubase or Studio One).",
              "File > Export DAWproject...", C::DawExported},
             {"Words you do not know are explained in the Terms tab, with a field to find them.",
              "Terms tab", C::None},
         },
         "Larger text: Ctrl+= (smaller Ctrl+-). Higher contrast: Ctrl+Shift+Up, or View > Contrast."},
    };
}

} // namespace

const std::vector<TutorialLesson>& tutorialLessons()
{
    static const std::vector<TutorialLesson> lessons = makeLessons();
    return lessons;
}

std::size_t tutorialStepCount()
{
    std::size_t n = 0;
    for (const auto& l : tutorialLessons()) n += l.steps.size();
    return n;
}

std::string tutorialMarkdown()
{
    std::string md;
    md += "# Getting started with mini-daw\n\n";
    md += "<!-- Made from src/core/src/tutorial.cpp: change the text there; the test T-UI-35 fails while this file "
          "differs (it writes the new text to the build folder). -->\n\n";
    md += "A short tutorial for beginners in four lessons. The same lessons are in mini-daw under **Help > Getting "
          "started**, where each step ticks itself off when you have done it. On a Mac, the shortcuts are Cmd instead "
          "of Ctrl (Cmd+R, Cmd+L, ...).\n\n";
    for (const auto& l : tutorialLessons()) {
        md += "## " + l.title + "\n\n";
        md += l.intro + "\n\n";
        int n = 0;
        for (const auto& s : l.steps) {
            md += std::to_string(++n) + ". " + s.text + "  \n   *Where:* " + s.where + "\n";
        }
        md += "\n";
        if (!l.tip.empty()) md += "**Tip:** " + l.tip + "\n\n";
    }
    return md;
}

} // namespace minidaw
