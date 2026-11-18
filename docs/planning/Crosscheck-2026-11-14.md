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

## Actions taken (user: "yes fix CC1–CC9, CC1 first")

| # | Action |
|---|--------|
| CC1 | A linked track: mute = own ∨ MIDI track's ∨ sound from the instrument; solo = own ∨ MIDI track's (as `playbackGain`). New test case with the four combinations (the old rule gives the wrong answer for the "muted" and "soloed" ones). |
| CC2 | `vst3DeviceId`: 32 hex digits → `XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX`, upper case. Byte order settled from the SDK: `UID::toString` writes the COM GUID fields on Windows and the bytes in order elsewhere, which are the same UUID fields, so dashes are all that is needed. Whether Bitwig / Cubase write upper or lower case is still to be seen in a real file (T-PRJ-10 M). The `.vstpreset` keeps 32 hex digits. |
| CC3 | A linked track's channel is stereo; an audio track's channel count is its WAV's. The main test now uses `channels = {0}` as `addLinkedSoundTrack` makes it; also a linked track without a take. |
| CC4 | Marks counted from the recording's earliest take that exists (map built before the tracks are written). Test: track 1 at 1 s, track 4 of the same recording at 0.95 s → mark at 1.45 s. While testing: the main test's shifted take sat in the same recording folder; it now has its own (another recording, as in real use). |
| CC5 | A note whose end falls in the same frame (also one held to the end) lasts one frame. |
| CC6 | `dawExportName` (trim, a typed ".dawproject" dropped, any case) and `dawExportFileName` (`safePresetName`: also CON / NUL …); the window says "Choose a folder for the file." / "Type a name for the file." and Export is off. Tested in core and by hand on Xvfb. |
| CC7 | Folders made for the file are removed when nothing was written (only empty ones); script case `exportfail` (a 300-character file name) checks it. Running on the UI thread: accepted (busy message shown, one-off action, audio keeps playing). |
| CC8 | New cases: a hand-made MIDI file (format 1, tempo change, overlapping notes of one key, a note-off without its note, a 0-length note, a second track), same-name takes and one take on two tracks. "T-PRJ-10: XSD" now runs all `[dawproject]` cases, unpacks each of the 5 exports with CMake's `file(ARCHIVE_EXTRACT)` (libarchive) and checks both XML files and that every `File` / `State` path is there and not empty. Script case `export`: `export.check.cmake` unpacks the file and checks the `.vstpreset` ("VST3" header); `run_script.cmake` includes a `<case>.check.cmake` when there is one. |
| CC9 | Spec: the tempo map is followed (no left-out line); notes, deviceID, channels, marks and window rules written down (v0.61). Plan PX row #22, P27a. |

Linux 236/236 (the XSD check with the lxml stand-in for xmllint); MinGW under Wine 220/220 Catch2 cases.

# Crosscheck #23 — 2026-11-16 (after P27b and P28: Auto record track, DAWproject import)

Scope: Spec v0.63, Plan, Tests, README, CLAUDE.md, code at `60582b8` (P27b `dbf5a6b`, P28 `380f0af`). Claude checked Spec ↔ Plan ↔ Tests ↔ docs. An independent reviewer, a subagent, also read the code. It was given the code, the Spec and the format's reference repository, but not Claude's conclusions. Claude checked these findings at the code:
- CC1: `dawproject.cpp:361` sets `timeUnit` on `Lanes` only, and `Markers` at `:623` has none. `Timeline.java` says "if no TimeUnit is provided by this or the parent scope then 'beats' will be used". The importer reads Markers with the Lanes' unit (`dawproject_import.cpp:309`).
- CC2: `fail` → `cleanUp(true)` at `:867` runs while `sources` is still open. `sources.clear()` is only on the success path, at `:1030`.
- CC4: `importDawProjectNow` (`main.cpp:2537`) compares the typed path. Open and New use `cleanFolder`.

| Area | Verdict |
|------|---------|
| Times (import) | ✅ `time` / `duration` in the parent's unit, `contentTimeUnit` for playStart / playStop / loops / content, `fadeTimeUnit`, Warps units, loop passes (the first from playStart, later ones from loopStart), notes cut at pass / playStop / clip end, marks from the earliest take; ⚠️ Markers' unit taken from Lanes **CC1** |
| Audio (import) | ✅ sinc kernel (DC gain 1 both ways, window exactly 24 crossings, table bounds), the same rate on whole frames copied exactly, mono to both sides, linear fades, the WAV reader's formats, the 24-bit write and its 4 GiB limit; ⚠️ no limit on a clip's place or length **CC3**; negative fade-in, clipping of summed clips, fade-out with playStop **CC7** |
| Notes / tracks (import) | ✅ velocity mapping, note-off before note-on, 1-frame minimum, SMF at 960 PPQ / 120 bpm as mini-daw plays it, `reference` clips; ⚠️ same-name lanes merged **CC6**; submix / VCA tracks, the loop-pass cap, notes sounding into playStart, empty lanes left out without a line **CC8** |
| Mixer, instruments | ✅ linear / dB volume, −60 … +6 dB with a line, Mute / solo, deviceID in any form, the class ID from the `.vstpreset` as a fallback, state bytes |
| Files, ZIP, cleanup | ✅ ZIP entry names never used as output paths (unpacked as `.unpacked/<n>.wav`), a folder that holds a project refused, only what the import made is removed; ⚠️ Windows: files still open when cleaning up **CC2**; `takes` left after an early failure **CC9**; `external` paths outside the file's folder, no size cap on XML / state entries **CC5** |
| GUI / script | ✅ the name follows the file's stem until it is typed over, Import off while recording, `startBusy`, the open project saved first, `import` takes 2 arguments, the `.dawproject` filter ignores case (ImGui list and Windows dialog); ⚠️ folder not made absolute / normal before the "outside the open project" check **CC4** |
| P27b export | ✅ the `writeClip` refactor keeps the old behaviour (playStart for a start before 0, a WAV shared by path, `audioChannels` from the WAV); the "Auto record" track is muted, has a lane per file and the right contentType, and is dropped when nothing is readable; ⚠️ Markers without `timeUnit` (since P27) **CC1** |
| Spec ↔ Plan ↔ Tests ↔ docs | ✅ SPEC-PRJ-05 ↔ P28 ↔ T-PRJ-11, SPEC-PRJ-04 (Auto record) ↔ P27b ↔ T-PRJ-10, RUN-02 `import`, UI-14 file mode, README, CLAUDE.md, Terms; ⚠️ text mismatches, the T-RUN-04 row, a wrong test ID in a result row, Plan PX row **CC10**; test gaps **CC11** |

Still open from P28, waiting for the user: SPEC-PRJ-05 first said a track's clips would be left out beyond the first. P28 merges them instead: all note clips go into one MIDI take and all audio clips are rendered into one WAV. The user has not confirmed this yet.

## Findings

| # | Sev | Finding | Proposed fix |
|---|-----|---------|--------------|
| CC1 | medium | The export writes `timeUnit="seconds"` on `<Lanes>` only. `<Markers>` is a sibling under `<Arrangement>`, so by the format its times are in **beats**: a marker at 2.4 s is read as 2.4 beats (1.2 s at 120 bpm) by Bitwig, Cubase and other programs. mini-daw's importer takes the Markers' unit from the Lanes, so the round trip hides the error. Present since P27. | Export: `timeUnit="seconds"` on `<Markers>`. Import: Markers default to the Arrangement's scope (beats), not the Lanes'. A test checks the attribute in the XML, and a hand-written file has Markers in beats. |
| CC2 | medium (Windows) | When an import fails after the files were unpacked (for example, the disk fills up), cleanup removes `takes/<take>` while the unpacked WAVs are still open (`sources`). On Windows open files cannot be deleted, so the take folder, `.unpacked` and the new project folder stay, against "removes what was written". | Close `sources` inside `fail` before `cleanUp`. Test a write failure (an unwritable take folder) on both systems. |
| CC3 | medium | There is no limit on a clip's `time` / `duration`. A clip at 10000 s writes ~2.9 GB of silence before it (the WAV starts at the first clip). A `duration` far longer than the file pads with silence. A huge or non-finite value is undefined behaviour in `llround`, or the program renders until the 4 GiB limit. | Check `isfinite` and range before converting. Without a loop, end a clip where its content ends. Leave out (with a line) clips beyond a few hours. |
| CC4 | low | The import folder is the typed text (`cleanPathText`), not `cleanFolder` as in Open / New. A relative path or one with `..` (script `import x.dawproject ../proj/sub`) can pass the "outside the open project" check and give a project inside a project. | `cleanFolder` in `importDawProjectNow` and `dawImportTarget`. |
| CC5 | low | `external` paths may be absolute or use `..`, so a file can copy any readable WAV / `.vstpreset` on the machine into the new project (reads only). `project.xml`, `metadata.xml` and state entries are read into memory with no size limit. | List an external path outside the file's folder as left out (or allow it with a line naming it); cap entries read into memory (e.g. 256 MB) with an error. |
| CC6 | low | Two nested lanes with the same name go into one track and are added together, against "lanes … stay apart". | Number duplicates ("Take 1", "Take 1 2"). |
| CC7 | low | Audio left out without a line: a negative `fadeInTime` (a cross-fade) is ignored; overlapping clips added beyond full scale are clipped silently; a clip that ends at `playStop` before its `duration` gets its fade-out at the `duration` end (reviewer; to confirm while fixing). | A line for cross-fades and for clipped samples (count); fade-out at the end of what plays. |
| CC8 | low | Tracks / notes left out without a line: submix / VCA tracks without children come in as empty tracks; loops of more than 100000 passes drop notes silently; a note that starts before `playStart` and sounds into it is dropped; a nested lane with neither notes nor audio is dropped. | Leave out submix / VCA with a line; a line when the pass cap is hit; a note sounding into the window starts at the window; a line for empty lanes (or keep them as empty tracks, as a track without clips). |
| CC9 | low | If making `takes/<take>` fails right after `createProjectFolder`, the new `takes` folder stays, so the new project folder is not removed either. | Remove `takes` too when it was new. |
| CC10 | doc | Spec SPEC-PRJ-05: release velocity says "1–127" but the code maps 0–127 (0 is valid); the window does not say a folder inside the open project is refused (only after Import, in the report); the note is "Imported from <file name>." with the extension. SPEC-PRJ-04 does not say whether the "Auto record" track is in the "N track(s)" count (it is not). Tests.md: the T-RUN-04 row lists neither `export` / `exportfail` nor `import`; the P28 result row says "T-UI-14 file mode" (T-UI-14 is the window place; it should be T-UI-21 / T-UI-22). Plan PX row #23. | Fix the texts; the window shows "Choose a folder outside the open project." with Import off; decide on the count (proposal: count it, report "… incl. Auto record"). |
| CC11 | low | Test gaps: `listFolder` with a file extension (the files list, case, hidden) is not tested (T-UI-21); no write-failure cleanup test; no import into / inside the open project; WAV inputs 16-bit, 32-bit, float, > 2 channels, 8-bit refused; hostile `external` paths and ZIP names with `../`; same-name lanes; volume above +6 dB; the Windows file dialog (T-UI-23 has folders only). | Add these cases (CC1–CC9 tests included); a Browse-for-a-file step in T-UI-23 (in `dist/TESTS.txt` T-PRJ-11 step 2 already). |

## Actions taken (user: "yes fix CC1–CC11, CC1 first"; P28b, version 0.28.2)

| # | Action |
|---|--------|
| CC1 | The export writes `timeUnit="seconds"` on `<Markers>`. The import takes the Markers' unit from the Arrangement's scope, not from the Lanes beside it. Files from mini-daw before 0.28.2, recognised by `Application`, are read as they were meant (seconds). Tests: the attribute is in the export; a file with Lanes in seconds and Markers without a unit gives beats; mini-daw 0.1.0 / 0.28.1 give seconds; 0.28.2 gives beats; the round trip runs at the current version. |
| CC2 | The source files are closed inside `fail` before anything is removed. Test: a write failure after one take, made with the test switch `detail::importFailAfterTakes`. A new folder is removed completely; an existing folder keeps the user's file. This runs on MSVC in CI, where open files cannot be deleted. |
| CC3 | Limits:<br>• Clips, warp points and notes more than 24 h from the start are left out with a line.<br>• A clip without a loop sounds until its content ends (the file or playStop).<br>• One track's audio spans at most 3 h from its first clip (less at high rates, since a WAV holds 4 GB); later clips are listed. |
| CC4 | The import folder goes through `cleanFolder` (absolute, `..` resolved) in the window and in the script command. Script case: `…/other/../imported/sub2` with "imported" open is refused. |
| CC5 | `external` files are read only from the `.dawproject` file's folder or below it. Absolute paths and `../` give "(outside the file's folder)", for audio and for instrument states. Entries read into memory, and external states, are limited to 256 MB. |
| CC6 | Nested lanes with the same name are numbered ("Take 1", "Take 1 2"). |
| CC7 | Changes:<br>• A negative fade time gives "its cross-fade: played without it".<br>• Samples above full scale are counted and listed.<br>• The fade-out ends at the clip's own end, not where its content stops.<br>Test: playStop 0.5 s with a 0.5 s fade-out in a 1 s clip plays the first 0.5 s at full level. |
| CC8 | Changes:<br>• A submix / VCA track without tracks is left out with a line.<br>• A loop of more than 100000 passes is cut with a line.<br>• A note still sounding at a clip's start (playStart) plays from there.<br>• A nested lane with nothing to import gets a line. |
| CC9 | Whether `takes` existed is now checked before `createProjectFolder` makes it. Until now the check came after, so a failed import never removed `takes`, and so never removed the new project folder either; the reviewer had seen only the early case. After an early failure, `takes` and the folder are removed when new. Test: a recording folder name of 300 characters leaves nothing behind. |
| CC10 | Spec texts:<br>• release velocity 0–127;<br>• the note text;<br>• the window rule;<br>• the "Auto record" track is counted in "N track(s)", with "(counted above)" in its line;<br>• the new limits and lines.<br>The import window says "Choose a folder outside the open project …" with Import off. Tests.md: the T-RUN-04 row lists `export` / `exportfail` / `import`, and the P28 result row's test ID is corrected. The Plan PX row (#23) was already done. |
| CC11 | New cases:<br>• `listFolder` with an extension (T-UI-21c);<br>• WAV formats 16 / 32-bit, float, 3 channels, 8-bit;<br>• a limits-and-lines file covering CC3 and CC5–CC8;<br>• write failure and early failure;<br>• Markers units;<br>• the script case `import` (inside the open project, also with `..`).<br>The Windows file dialog step was added to T-UI-23. Not tested: the 256 MB limit (it would need a file that large). |
