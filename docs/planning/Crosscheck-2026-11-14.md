# Crosscheck #21 — 2026-11-14 (after P26 and P26a: reopen in the same state, window placement)

Scope: Spec v0.57, Plan, Tests, README, CLAUDE.md, code at `b6eec8c` (changes since `3af6771`: P26 `a4a4d94`, P26a `b6eec8c`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Claude checked at the code: CC1 (`rememberProjectInConfig`, `main.cpp:2149`, writes `last` without a script check), CC2 (`rememberAutoRecord`, `main.cpp:2620`, resets the whole `SessionState`), CC3 (`saveConfigFile` → `writeFileReplacing(..., true)` from `toggleAuto` on the UI thread), CC4 (`presets.cpp:407` `fs::rename` over the path). The reviewer's point that an editor of a track replaced by Open take folder could be restored on another track is **refuted**: `openTakeFolder` calls `unloadAllInstruments`, which closes every editor window (`main.cpp:1610`, `4541`).

| Area | Verdict |
|------|---------|
| `[state]` in the config (T-UI-29) | ✅ parse with warnings (bool, position incl. NaN, editors deduplicated and capped at 16, place range, window ids lower-cased, unknown warned), save / read back, default text parses without warnings, written beside the old file and synced; ⚠️ a symlinked `config.ini` replaced by a file **CC4**; test gaps **CC7** |
| Auto record kept | ✅ only the button (and the restore) switch it, saved at once; scripts cannot switch it; a crash after switching project refused with "Not restored"; `autoRestoreFailed` only for the same project; ⚠️ the reset on another project also drops the own windows **CC2**; a synced write on the UI thread right after auto record starts **CC3** |
| Capture / restore (T-UI-30) | ✅ captured at a normal close before anything stops, restored once in the first frame after project, instruments, stream and MIDI ports; Project note only with a project; ⚠️ a script run with the normal config changes `last`, so the next start loses the state **CC1**; a minimised editor's place (Windows) **CC5**; position with no stream (48000 assumed) **CC6** |
| Instrument window place | ✅ multi-monitor and negative coordinates, at least 100 × 50 px on a screen, else beside the main window |
| Window placement (P26a, T-UI-31) | ✅ `FindWindowByName` hashes `###` titles as `Begin` does; windows begun in the same frame, also hidden in their first frame, counted; child / tooltip / popup / main / fallback windows not; ImGui's ini off, so `Appearing` is not overridden; test gaps **CC7** |
| Status line | ✅ "Project warning:", no other "Project note:" left |
| Spec ↔ Plan ↔ Tests ↔ docs | ✅ SPEC-UI-09a traced to P26 / P26a and T-UI-29..31; README, CLAUDE.md, Terms "Auto record"; ⚠️ T-UI-31's result row says M pass, but the "Project warning" label was not looked at by hand; Plan P26a without "Windows by the user"; Plan PX row **CC8** |

## Findings

| # | Sev | Finding | Proposed fix |
|---|-----|---------|--------------|
| CC1 | medium | A `--script` run with the normal config (possible when mini-daw is not running) that opens another project writes `[project] last` (`rememberProjectInConfig`). The next normal start opens the script's project; auto record, position and editors of the real one are "Not restored" and overwritten at the next close. Spec: scripts "neither restore nor save `[state]`" — true, but `last` belongs to the same picture. | In a script run, do not change `last` / Recent (the script's project is not the user's choice); test in a script case. |
| CC2 | low | `rememberAutoRecord` resets the whole state when the project differs, also `windows`, which the Spec says always come back. Normal close in A, open B, switch auto record, crash → the own windows of the last close are gone. | Reset only auto record, position and editors; keep `windows`. |
| CC3 | low | Switching auto record writes and syncs `config.ini` on the UI thread right after auto record started; a slow disk delays the first frames (the audio thread is not affected; a part start uses a look-back, so at most a "noticed late" note). | Save before starting, and only keep the new value when the start worked (or write it a frame later). Measure first; may need no change. |
| CC4 | low | Saving the config renames a new file over `config.ini`: a symlinked config (dotfiles) becomes a normal file and the link is gone; the file's mode is not kept. Before P26 the file was overwritten in place. | Write beside the link's target (resolve the link first). |
| CC5 | low | Windows: closing mini-daw with the main window minimised (editors minimised with it) saves the editor at about (-32000, -32000); at the next start it is refused and the editor opens beside the main window, the real place lost. | Skip a minimised editor's position (`GLFW_ICONIFIED`) and keep its last normal place. |
| CC6 | low | Without a running stream the position is converted with 48000 Hz at both ends; if the stream later runs at 44100 the restored position is off. (Without a stream the position is 0 in practice.) | Do not save or restore a position without a running stream. |
| CC7 | low | Test gaps: T-UI-29 lacks the 16-editor cap, `position` 1e7 / "nan" / "inf" / "1e400", `+3` and `3:1`, a `project` with quotes, backslashes and non-UTF-8 bytes read back, a second `[state]` section or key; the restore / auto record decisions live in `main.cpp` and are only tested by hand. T-UI-31: no popup / NoTitleBar window present, no `###` title opened twice, no many-windows wrap. | Add those cases; move the restore decision (what comes back for which project) into a pure core function with a test. |
| CC8 | doc | T-UI-31 result row (label not looked at by hand); Plan P26a "Windows by the user (T-UI-30, T-UI-31)"; Plan PX row #21. | Update the texts. |

CI run 43599860783 (`b6eec8c`): windows, linux, licence green; Linux 222/222, Windows 212/212.

## Actions taken (user: "yes fix CC1–CC8, CC1 first")

| # | Action |
|---|--------|
| CC1 | `rememberProjectInConfig` does nothing in a script run (no `last`, no Recent). `run_script.cmake` checks for every script case that `config.ini` does not name the script's project; without the fix `basic` and `failures` fail. |
| CC2 | `switchAutoRecordState` (core): another project drops position and editors, keeps `windows`; tested. |
| CC3 | No change: a synced write of the config takes 3–6 ms here, and auto record only cuts a part's start when the program is late by more than the audio it keeps in memory (seconds, T-REC-34); this write cannot cause that. |
| CC4 | `writeFileReplacing` follows symbolic links (the target is replaced, the link stays) and gives the new file the old one's permissions; tested on Linux with a relative link. |
| CC5 | `PluginWindows` keeps each editor's last place while not minimised (`GLFW_ICONIFIED`, or Win32's -32000) and gives that at close. By reading only (a minimised window is not reachable on Xvfb). |
| CC6 | Position saved only with a running stream (else the last one of the same project is kept), restored only with one ("Not restored: the song position (no audio stream)"); the 48000 Hz fallback is gone. |
| CC7 | The decisions moved to core (`switchAutoRecordState`, `sessionStateAtClose`, `planRestore`) and tested; T-UI-29 edge cases (editor cap and forms, positions, a second section, a project path with quotes / backslashes / a non-UTF-8 byte); T-UI-31: popup and main window, a `###` window under another title, 30 windows. |
| CC8 | "Project warning" looked at on Xvfb (result row); Plan P26a "Windows by the user (T-UI-31)"; Plan PX row (done with the crosscheck). |

Spec v0.58, Plan P26b.

---

# Crosscheck #22 — 2026-11-14 (after P27: DAWproject export)

Scope: Spec v0.60, Plan, Tests, README, CLAUDE.md, THIRD_PARTY.md, code at `f8cbe9f` (P27 `9d000c2`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code, the Spec and the format's reference repository, not Claude's conclusions). Claude checked at the code: CC1 (`dawproject.cpp:349-362` against `playbackGain`, `track.cpp:195`), CC2 (`Device.java:39-43`), CC3 (`addLinkedSoundTrack` sets `channels = {0}`, `track.cpp:157`; `render.cpp:57` writes 2 channels), CC4 (`dawproject.cpp:485`).

| Area | Verdict |
|------|---------|
| Structure, schema (T-PRJ-10) | ✅ element order valid, Arrangement / Lanes(timeUnit) / Lanes(track) / Clips / Clip as in the reference, `destination` → master channel, ids unique; ⚠️ VST3 `deviceID` not in the UUID form the format asks for **CC2** |
| Times | ✅ `time` / `duration` in seconds, `playStart` in the content's units, a take shifted before 0 cut correctly, another sample rate in `Audio`; markers per recording ⚠️ **CC4** |
| Notes | ✅ FIFO pairing per channel and key, velocity-0 note-off, running status, notes held to the end, the SMF's tempo map followed (as in playback); ⚠️ zero-length notes **CC5** |
| Mixer | ✅ linear gain 0..2, silent at −60 dB; ⚠️ linked sound track's mute / solo do not follow its MIDI track **CC1**; a rendered linked track exported as mono **CC3** |
| ZIP / files | ✅ UTF-8 entry names (miniz flag), zip64 for large files, unique entry names, `.tmp` + rename, nothing left on failure, `_wfopen` on Windows |
| GUI / script | ✅ not while recording, not without tracks, report and log lines, script `ok` only on "Written:"; ⚠️ empty folder, ".dawproject" typed in the name, blank name **CC6**; export blocks the window for large takes **CC7** |
| Spec ↔ Plan ↔ Tests ↔ docs | ✅ SPEC-PRJ-04 ↔ P27 ↔ T-PRJ-10, RUN-02 `export`, README, CLAUDE.md, THIRD_PARTY.md, Terms "DAWproject"; ⚠️ Spec says a MIDI file's tempo map is left out "with a line", but it is followed and no line exists; Plan PX row **CC9** |

## Findings

| # | Sev | Finding | Proposed fix |
|---|-----|---------|--------------|
| CC1 | medium | A linked "… sound" track's mute and solo are exported from the linked track alone. mini-daw (`playbackGain`) also silences it when the MIDI track is muted, and solos it when the MIDI track is soloed. With "Sound from: recording": a muted MIDI track → the other DAW still plays the sound; a soloed MIDI track → exported muted + soloed, the linked track not soloed → the other DAW plays nothing. | Linked track: mute = own mute ∨ owner mute ∨ sound from instrument; solo = own solo ∨ owner solo. A MIDI track muted for "Sound from: recording" exported without solo. Test cases for both. |
| CC2 | medium | `Vst3Plugin deviceID` is written as 32 hex digits; the format (`Device.java`) asks VST3 for "the canonical textual representation of the UUID (8-4-4-4-12 with no braces)". A strict importer may not find the plugin. Uncertain: the byte order (VST3 class IDs are COM-ordered on Windows). | Write 8-4-4-4-12 from the class ID's bytes in order; confirm with a file exported by Bitwig or Cubase (T-PRJ-10 M: user's help needed). The `.vstpreset` keeps its 32-hex form (that format's own). |
| CC3 | low | A track's `audioChannels` comes from its input channels; a linked track has `channels = {0}` but its WAV (recorded or rendered) is stereo → `Channel audioChannels="1"` with a 2-channel clip; a DAW may make it a mono track. Test hides it (`channels = {}`). | Take the count from the WAV (read before the channel is written); linked track test with `{0}`. |
| CC4 | low | Marks of a recording are placed using the `takeStart` of the first track in the list that plays it; tracks of one recording can differ by their latency correction (tens of ms), so the markers can be off that much against the other tracks; on mini-daw's own Timeline marks are placed by the recording's start. | Use the same reference as the Timeline does (the recording's start), i.e. the smallest `takeStart` of its tracks; a test with two tracks at different starts. |
| CC5 | low | A note whose on and off fall on the same frame is written with `duration="0"`; some programs drop such notes. | Give it one frame of length (as a player hears it); test. |
| CC6 | low | Export window: an empty "In folder" writes into the program's current folder; a name typed with ".dawproject" gives "x.dawproject.dawproject"; a name of spaces passes (file "track", empty title). | Export disabled with an empty folder (with a line saying so); a trailing ".dawproject" dropped; name trimmed. |
| CC7 | low | The export copies all takes on the UI thread (busy message shown): with GBs of takes the window stops responding for a while; a failed export leaves the folder it made. Audio keeps playing (own thread). | Accept for now (one-off action, busy message), or move the write to a worker thread later; remove a folder the export made when nothing was written. |
| CC8 | low | Test gaps: the ZIP read back only with miniz (no second reader); same-name takes (`uniqueZipName`) and one take on two tracks; overlapping same-key notes, a note-off without a note, a tempo change in the file, several SMF tracks; the script case does not check that the `.vstpreset` is in the file; XSD check of one scenario only. | Add those cases; check the ZIP also with CMake's `file(ARCHIVE_EXTRACT)` in the script case and look for the preset there. |
| CC9 | doc | Spec SPEC-PRJ-04: "a MIDI file's tempo map beyond what mini-daw plays" listed as left out — it is followed, nothing is left out; Plan PX row #22. | Fix the text. |

CI run 43627670881 (`9d000c2`): windows, linux, licence green; Linux 231/231, Windows 219/219.
