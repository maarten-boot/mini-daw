#include "minidaw/terms.hpp"

#include <cctype>

namespace minidaw {

namespace {

const std::string kBasics = "Basics";
const std::string kRecording = "Recording";
const std::string kPlaying = "Playing";
const std::string kInstruments = "Instruments";
const std::string kProjects = "Projects and files";
const std::string kSound = "Sound and audio setup";

std::vector<Term> makeTerms()
{
    return {
        // --- Basics ---------------------------------------------------------
        {"mini-daw", kBasics,
         "This program: a small recorder for audio (a microphone, a guitar, any sound input) and MIDI (a keyboard). "
         "\"DAW\" means digital audio workstation, a program to record and play music. Only one mini-daw runs per "
         "user: starting it again brings the running one to the front.",
         "", {"Track", "Take", "Project"}},
        {"Track", kBasics,
         "One line in the track table on the Recording tab. A track records one input: an audio track records one or "
         "two audio input channels, a MIDI track the notes from one MIDI input. Each track holds at most one take, "
         "which it plays back. Tracks have a volume, Mute (M) and Solo (S).",
         "Recording tab, the track table; Add audio track, Add MIDI track", {"Audio track", "MIDI track", "Take"}},
        {"Audio track", kBasics,
         "A track that records sound from the audio input device: one channel (mono) or two (stereo). Its take is a "
         "WAV file.",
         "Recording tab: Add audio track; the Input column", {"Track", "Audio device", "WAV file"}},
        {"MIDI track", kBasics,
         "A track that records MIDI: the notes and controls (keys, pedal, wheels) played on a MIDI keyboard, not the "
         "sound itself. Its take is a MIDI file. To hear it, choose an instrument or a MIDI output in its Output column.",
         "Recording tab: Add MIDI track; the Input and Output columns", {"Track", "MIDI", "Instrument", "MIDI output"}},
        {"Take", kBasics,
         "One recording of one track: the file made between pressing Record and Stop. All armed tracks record at the "
         "same time, so one Record makes one take per armed track, all in the same take folder. The Take column shows "
         "which take a track plays.",
         "Recording tab: the Take (plays) column; Timeline tab", {"Take folder", "Record", "Arm"}},
        {"Recording", kBasics,
         "On the Timeline tab: everything one Record (a take folder) or one auto record part made, shown as one block. "
         "A recording can have a name, a note and marks (right-click it).",
         "Timeline tab", {"Take folder", "Auto record part", "Mark"}},

        // --- Recording ------------------------------------------------------
        {"Arm", kRecording,
         "Ticking a track's Arm box gets it ready to record: Record then records every armed track. An armed track "
         "shows its input level and can be monitored; a MIDI track's input port is opened only while it is armed. "
         "Tracks that are not armed play their takes during a recording (overdub).",
         "Recording tab: the Arm column", {"Record", "Monitor", "Overdub"}},
        {"Record", kRecording,
         "Starts recording all armed tracks (Ctrl+R starts and stops it). Stop ends it; each armed track then has a new "
         "take. The line under the transport shows the time and how much was written.",
         "The transport: Record, Stop", {"Arm", "Take", "Take folder", "Overdub"}},
        {"Take folder", kRecording,
         "The folder one Record makes, named by its date and time (take-20261109-153000). It holds one file per armed "
         "track (01-Piano.mid, 02-Vocals.wav, ...) and, if there are notes, recording.json. Take folders are in the "
         "takes folder: the project's takes folder when a project is open, otherwise the recording folder.",
         "File > Open take folder...; Settings > Recording...", {"Take", "Takes folder", "Recording folder"}},
        {"Takes folder", kRecording,
         "Where take folders and auto record parts are kept: \"takes\" in the project's folder when a project is open, "
         "otherwise the recording folder. The Timeline tab shows what is in it; deleted recordings go to its "
         "\"deleted\" folder.",
         "Timeline tab", {"Take folder", "Recording folder", "Project"}},
        {"Recording folder", kRecording,
         "Where takes go when no project is open. Default: Documents\\mini-daw on Windows, ~/Music/mini-daw on Linux.",
         "Settings > Recording...", {"Takes folder", "Project"}},
        {"Auto record", kRecording,
         "Records by itself when you play: while it is on, mini-daw waits, and a part starts at the first note or "
         "control from an armed MIDI track, or when an armed audio track gets louder than the threshold. The part ends "
         "after a pause (5 s by default). Nothing is written while you do not play. If it is on when mini-daw closes "
         "(or crashes), it is on again at the next start.",
         "The transport: Auto record; Settings > Recording...", {"Auto record part", "Threshold", "Arm"}},
        {"Auto record part", kRecording,
         "What auto record made from one stretch of playing: a folder in \"auto\" in the takes folder, named by date and "
         "time, with one file per armed track. It starts 1 s before the first note and ends 1 s after the last. Parts "
         "do not change the tracks; File > Open take folder... loads one.",
         "Timeline tab: the \"Auto record parts\" row", {"Auto record", "Take folder"}},
        {"Threshold", kRecording,
         "How loud an audio input must be to start an auto record part: -40 dBFS by default (-60 to -20).",
         "Settings > Recording...", {"Auto record", "dBFS"}},
        {"Monitor", kRecording,
         "Hearing an audio track's input on the output while it is armed, for example to hear yourself through "
         "headphones. Switch it off when the input is already heard another way (or it sounds twice).",
         "Recording tab: the Monitor column", {"Arm", "Latency"}},
        {"Overdub", kRecording,
         "Recording while other tracks play: Record plays the takes of the tracks that are not armed, from the song "
         "position, and the armed tracks record along. The new takes are put in time with what you heard (moved "
         "earlier by the latency).",
         "Recording tab: the line under the transport says how many tracks will play along",
         {"Record", "Arm", "Latency", "Song position"}},
        {"Note", kRecording,
         "A few words kept with a recording (right-click it on the Timeline tab > Notes...) or with the project "
         "(File > Project note...). They are in recording.json in the take folder, or in project.json.",
         "Timeline tab; File > Project note...", {"Mark", "Recording"}},
        {"Mark", kRecording,
         "A note typed while recording, kept with its moment in the recording (\"Note at 01:23.4\"). Marks show as small "
         "ticks on the recording's block on the Timeline tab.",
         "The transport while recording: Note at ...; Timeline tab", {"Note", "Recording"}},

        // --- Playing --------------------------------------------------------
        {"Transport", kPlaying,
         "The buttons that start and stop: Play, Stop, Record, Auto record and |< Start (back to the beginning; the "
         "Home key), with the song position and what is going on. It is on the Recording and Timeline tabs.",
         "Recording and Timeline tabs, at the top", {"Song position", "Record", "Auto record"}},
        {"Song position", kPlaying,
         "Where in time playing starts: |< Start (or the Home key) sets it to the beginning; Stop keeps the place "
         "where it stopped.",
         "The transport", {"Transport", "Overdub"}},
        {"Timeline", kPlaying,
         "The Timeline tab shows every recording in the takes folder in time order: auto record parts on one row, "
         "Record takes on the other. Long pauses are drawn short, with their length. Click a recording to hear it; "
         "\"Load onto the tracks\" puts its takes on the tracks; right-click for Rename, Notes and Delete.",
         "Timeline tab", {"Recording", "Takes folder", "Mark"}},
        {"Mute and solo", kPlaying,
         "M (mute) silences a track. S (solo) plays only the soloed tracks; mute wins over solo. Both work while "
         "playing and recording.",
         "Recording tab: the Volume, mute, solo column", {"Track"}},
        {"MIDI output", kPlaying,
         "A MIDI port (a synthesizer, another program) that a MIDI track sends its take to, instead of an instrument.",
         "Recording tab: a MIDI track's Output column", {"MIDI track", "Instrument"}},

        // --- Instruments ----------------------------------------------------
        {"Instrument", kInstruments,
         "A software instrument (a VST3 plugin, such as a piano or a synthesizer) that turns a MIDI track's notes into "
         "sound, live and when playing. Choose it in the MIDI track's Output column; Settings > Instruments (VST3)... "
         "finds the instruments on the computer (Scan).",
         "Recording tab: a MIDI track's Output column; Settings > Instruments (VST3)...",
         {"VST3", "Linked sound track", "Edit", "Preset"}},
        {"VST3", kInstruments,
         "The plugin format mini-daw can load (files ending in .vst3). Plugins in other formats (VST2, AU, CLAP) are "
         "not offered.",
         "Settings > Instruments (VST3)...", {"Instrument"}},
        {"Linked sound track", kInstruments,
         "The audio track \"<name> sound\" right below a MIDI track with an instrument: it holds the instrument's sound "
         "as a WAV take, recorded live or made by Render. It has no input of its own.",
         "Recording tab, below the MIDI track", {"Sound from", "Render", "Instrument"}},
        {"Sound from", kInstruments,
         "On a linked sound track: \"instrument\" lets the instrument play the MIDI take (you hear changes to its "
         "sound); \"this recording\" plays the recorded WAV instead.",
         "Recording tab: the linked sound track's Input column", {"Linked sound track", "Render"}},
        {"Render", kInstruments,
         "Makes the instrument's sound of a MIDI take into a WAV file, faster than real time, without playing it. The "
         "file becomes the linked sound track's take; earlier files are kept.",
         "Recording tab: a MIDI track's Output column", {"Linked sound track", "Instrument"}},
        {"Edit", kInstruments,
         "Opens the instrument's own window with its knobs and settings. Close closes it. It can stay open while you "
         "play.",
         "Recording tab: a MIDI track's Output column", {"Instrument", "Preset"}},
        {"Program", kInstruments,
         "A sound built into an instrument (\"Grand Piano\", \"Strings\"). An instrument may have a list of them.",
         "Presets menu: Programs", {"Preset"}},
        {"Preset", kInstruments,
         "A saved sound of an instrument: a .vstpreset file, from the plugin's maker or saved by you (Presets > Save "
         "preset...). The Presets menu lists the programs and the preset files; < and > step through them. The sound "
         "is also kept in the project.",
         "Recording tab: a MIDI track's Output column, Presets", {"Program", "Instrument", "Project"}},

        // --- Projects and files ---------------------------------------------
        {"Project", kProjects,
         "A folder for one piece of work: project.json (the tracks, their settings, instruments and sounds, and the "
         "project note) and a takes folder with all its recordings. Without a project the tracks are kept only while "
         "mini-daw runs.",
         "File > New project..., Open project..., Save project as...", {"Takes folder", "Note"}},
        {"WAV file", kProjects,
         "The file of an audio take (.wav): 24-bit sound at the stream's sample rate. Any audio program can open it.",
         "In the take folder", {"Take", "Sample rate"}},
        {"MIDI file", kProjects,
         "The file of a MIDI take (.mid): the notes and controls with their times. Any music program can open it.",
         "In the take folder", {"Take", "MIDI"}},
        {"Deleted", kProjects,
         "Delete on the Timeline tab does not throw a recording away: it moves its folder to \"deleted\" in the takes "
         "folder. Move it back to get it again.",
         "Timeline tab: right-click a recording > Delete...", {"Takes folder", "Timeline"}},
        {"Log", kProjects,
         "A list of what mini-daw did and what went wrong, with times. It also says why a click on a greyed-out "
         "button did nothing. Useful when something does not work.",
         "View > Log (Ctrl+L)", {}},

        // --- Sound and audio setup ------------------------------------------
        {"MIDI", kSound,
         "Musical Instrument Digital Interface: messages such as \"key 60 down, this hard\" or \"pedal down\", sent by "
         "a keyboard. MIDI is not sound: an instrument or a synthesizer makes the sound from it.",
         "", {"MIDI track", "Instrument"}},
        {"Audio device", kSound,
         "The sound card or audio interface: its input (microphone, line in) feeds the audio tracks, its output "
         "(speakers, headphones) plays everything.",
         "Audio setup tab; Settings > Audio devices...", {"Audio track", "Sample rate", "Buffer"}},
        {"Audio stream", kSound,
         "The running connection to the audio device. Nothing is heard or recorded while it is stopped; the Recording "
         "tab then says so, with an Audio setup button.",
         "Audio setup tab", {"Audio device", "Dropout"}},
        {"Sample rate", kSound,
         "How many times per second the sound is measured: 48000 (48 kHz) or 44100 are usual. Takes are recorded at "
         "the stream's rate; a take at another rate is not played.",
         "Audio setup tab; Settings > Audio devices...", {"Buffer", "WAV file"}},
        {"Buffer", kSound,
         "The block of sound handled at once, in frames (for example 256). A small buffer gives less latency but needs "
         "a fast computer; a too small one gives dropouts.",
         "Settings > Audio devices...", {"Latency", "Dropout", "Sample rate"}},
        {"Latency", kSound,
         "The delay between a sound going in (or a key pressed) and hearing it: a few buffers. Overdubs are moved "
         "earlier by it, so they line up.",
         "Audio setup tab; Settings > Recording...: latency correction", {"Buffer", "Overdub", "Monitor"}},
        {"Dropout", kSound,
         "A moment the computer was too late with a buffer: a click or a short gap. Counted on the Recording tab and "
         "the Audio setup tab. A larger buffer helps.",
         "Recording tab: the audio line", {"Buffer", "Audio stream"}},
        {"dBFS", kSound,
         "Level in decibels below the loudest a digital sound can be: 0 dBFS is the top, -6 dBFS is about half, "
         "-60 dBFS is very quiet. The meters show it.",
         "Recording tab: the Level / activity column; the output meter", {"CLIP", "Meter"}},
        {"Meter", kSound,
         "The bar that shows a level as it changes, from -60 to 0 dBFS, with the highest recent peak as a line and as "
         "a number.",
         "Recording tab", {"dBFS", "CLIP"}},
        {"CLIP", kSound,
         "Shown after a sound reached the very top (0 dBFS): it was too loud and is cut off, which sounds harsh. Turn "
         "the input down and record again. Click CLIP to clear it; Record clears it too.",
         "Recording tab: beside a meter", {"dBFS", "Meter"}},
        {"Contrast", kSound,
         "How far text stands out from its background. mini-daw keeps every colour pair at least at the chosen "
         "minimum (AA 4.5:1 by default; AAA, Max or your own).",
         "View > Contrast; Ctrl+Shift+Up / Down", {}},
    };
}

char lowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool containsNoCase(std::string_view text, std::string_view part)
{
    if (part.empty()) return true;
    if (part.size() > text.size()) return false;
    for (std::size_t i = 0; i + part.size() <= text.size(); ++i) {
        std::size_t k = 0;
        while (k < part.size() && lowerAscii(text[i + k]) == lowerAscii(part[k])) ++k;
        if (k == part.size()) return true;
    }
    return false;
}

std::string_view trimmed(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

} // namespace

const std::vector<std::string>& termGroups()
{
    static const std::vector<std::string> groups = {kBasics, kRecording, kPlaying, kInstruments, kProjects, kSound};
    return groups;
}

const std::vector<Term>& terms()
{
    static const std::vector<Term> all = makeTerms();
    return all;
}

std::vector<std::size_t> findTerms(std::string_view query)
{
    query = trimmed(query);
    const auto& all = terms();
    std::vector<std::size_t> byName;
    std::vector<std::size_t> byText;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Term& t = all[i];
        if (containsNoCase(t.name, query)) {
            byName.push_back(i);
        } else if (containsNoCase(t.text, query) || containsNoCase(t.where, query)) {
            byText.push_back(i);
        }
    }
    byName.insert(byName.end(), byText.begin(), byText.end());
    return byName;
}

std::string trimmedTermQuery(std::string_view query) { return std::string(trimmed(query)); }

std::size_t termIndex(std::string_view name)
{
    const auto& all = terms();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const std::string& n = all[i].name;
        if (n.size() != name.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < n.size() && same; ++k) same = lowerAscii(n[k]) == lowerAscii(name[k]);
        if (same) return i;
    }
    return all.size();
}

} // namespace minidaw
