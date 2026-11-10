# Crosscheck #16 — 2026-11-09 (after PL2: preset menu)

Scope: Spec v0.43, Plan, Tests, README, CLAUDE.md, code at commit `f8cf624` (changes since `911abd0`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Claude checked the findings at the lines named: X1 (`main.cpp:1464-1469`: a project restore sets the state before the rack has the instrument; `unloadInstrument` takes it out of the rack first; Load preset sets it while the audio thread processes it), X12 (the "size" test byte is the offset's top byte), X13 (comment moved) confirmed. CI run 42532989769: Linux 187/187, **Windows 187/188** (X0); licence green.

| Area | Verdict |
|------|---------|
| .vstpreset format | ✅ same layout as the SDK (header, list, entries), bounds safe, 128 entries as the SDK; class-ID text as `FUID::toString` on Windows and Linux; ⚠️ class-ID text not checked to be hex **X7** |
| Program list | ✅ normalized value, root unit preferred, controller on the GUI thread, no audio-thread change; ⚠️ lists that do not match the parameter's steps **X5** |
| Loading a preset | ❌ state set while the audio thread processes the instrument **X1**; ⚠️ no recording guard **X6**; controller results ignored **X7** |
| Menu / stepping | ⚠️ same label twice strands < / > **X2**; saved label ≠ file name **X3**; stepping does not start at the marked program **X11**; cache **X9** |
| Save preset | ⚠️ Enter bypasses the disabled button (no user folder) **X4**; window not tied to one instrument, disk work per frame **X10**; Windows device names, error reasons **X8** |
| Tests | ❌ Windows: a test makes a non-UTF-8 file name **X0**; ⚠️ gaps **X12** |
| Spec ↔ code ↔ docs | ⚠️ **X13**, SPEC-VST-06a does not mention the preset name |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| X0 | high (CI red) | T-VST-35 fails on Windows: the test makes `café.vstpreset` from a narrow string literal, which MSVC converts with the ANSI code page (file `cafÃ©`); the listing reports that name correctly. Test defect (mini-daw converts names with `pathFromUtf8`); MinGW under Wine converts differently, so it was not seen. | Make test paths with non-ASCII names through `pathFromUtf8` (also the other new tests). |
| X1 | medium | `loadPreset` calls the plugin's `setState` while the audio thread runs `process()` on it; the project restore never does (state before the rack). A plugin that swaps tables in `setState` can click or crash on Load preset during playback or monitoring. | Take the instrument out of the rack (as `unloadInstrument`), set the state, put it back; same for nothing else (programs are parameter changes, which are made for this). |
| X2 | low-medium | `<` / `>` find the current entry by its label: the same name twice (a file in the user and the shared folder, or a program and a file) makes later entries unreachable and marks both. | Keep the chosen entry's index / path per track (session only), label as fallback; mark one entry; the stepping and labels moved into core and tested. |
| X3 | low | After Save the label is the typed name, the file `safeFileName(name)`: "Warm: pad" is saved as "Warm_ pad" and not found / marked afterwards; `cleanPathText` (a path cleaner) strips quotes and a trailing `/`. | Label = the file's stem; trim spaces only. |
| X4 | low | Enter in the Save window saves even when the button is disabled (no user preset folder: error "cannot make the folder : Invalid argument"); the menu shows an empty "Saved in". | One condition for both; a clear message when the user folder is unknown. |
| X5 | low | `programs()` may take another unit's list when the parameter's unit has none; a failed `getProgramName` gives an empty row; entries beyond `stepCount` (or with `stepCount` 0) cannot be chosen; `currentProgram` 0 for `stepCount` 0. | Use the unit's list only; "Program N" for a missing name; offer only entries the parameter can reach; tests with a second synth class or a mock. |
| X6 | low | Loading a preset file or a program has no guard while recording (the instrument choice has, SPEC-UI-16): the sound changes in the middle of a take without a note. | Rule: allowed while recording (like turning a knob in the plugin), logged with "while recording"; written in the Spec. Render unaffected (it uses its own copy). |
| X7 | low | `setState` ignores the controller's `setComponentState` / `setState` results (half-applied preset reported as success); `decodeVstPreset` does not check the class ID is 32 hex characters (`FUID::fromString` then reads an uninitialised value). | Check hex; report a controller refusal as a warning (the processor state counts). |
| X8 | low | Windows device names ("CON", "NUL", "COM1") as preset, vendor or plugin names; very long names; `writeFileReplacing` drops the system's reason. | Reserved names prefixed with `_`, names cut at 120 characters, the system message in errors. (Windows part unverified.) |
| X9 | low | `<` / `>` use the cached list (files added elsewhere unseen until the menu opens again); the 2000-files note also shows when the 50000-entries limit hit; listing runs on the GUI thread without the busy message. | Re-read the list when it is older than a few seconds; separate notes; listing through the busy message when it takes long is not needed (limits) — note in the Spec. |
| X10 | low | The Save window checks only that the track has some instrument (a reloaded or changed one would be saved under the old default name) and computes folders and `exists` every frame. | Remember the instrument's serial; close the window when it changes; compute the path when the name changes. |
| X11 | low (Spec) | Before a choice in mini-daw the menu marks the plugin's current program, but `<` / `>` start at the first / last entry (Spec: "from the current entry"). | Start from the plugin's current program when nothing was chosen here. |
| X12 | tests | The "size outside the file" case changes the offset's top byte, not the size (branch untested); nothing proves "compatible with other hosts" although the SDK is in the build tree; program-list variants untested. | Fix the byte; a test that reads a file written by the SDK's `PresetFile::savePreset` (with Info and Prog chunks) and has the SDK read ours (the hosting library gets `vstpresetfile.cpp` for the tests only if it builds without more SDK parts, else a byte-exact fixture); tests for the stepping logic in core. |
| X13 | docs | A comment of crosscheck #15 (W8) moved onto `savePresetError` (`main.cpp:116`); a wrong-typed `preset` in project.json warns about "module or name"; SPEC-VST-06a does not mention the preset name; which vendor name other hosts use for the folder is unverified (class's, then factory's). | Fix the comment and the warning; Spec. |

## Actions taken (user: "yes fix X0–X13, X0 first")
| # | Done |
|---|------|
| X0 | Non-ASCII test file names made with `pathFromUtf8` (T-VST-34, T-VST-35, the instrument preset test); MSVC to be confirmed by CI. |
| X1 | `withInstrumentOutOfRack`: a preset file's state is set with the instrument out of the rack (the rack waits until the audio thread lets go), then it is put back; silent meanwhile. GUI: Soft, then the file, no errors. |
| X2 | Entries have an identity (`p:<index>`, `f:<path>`); the track remembers the one chosen last (this session); one entry marked; stepping and labels moved into core (`presetChoices`, `findPresetChoice`, `stepPresetChoice`) and tested (same name twice). |
| X3 | Names only trimmed; after Save the label is the file's stem. GUI: "Warm: pad" → `Warm_ pad.vstpreset`, listed and marked. |
| X4 | One rule for Save and Enter (a name and a user folder); "No folder for your own presets" in the menu, Save preset disabled then. |
| X5 | Only the parameter's unit's list (another only when it is the only one); "Program N" for a missing name; only reachable entries; continuous parameters use the list's size (core arithmetic, tested). |
| X6 | Allowed while recording; "(while recording)" in the log; Spec. |
| X7 | Class ID must be 32 hex characters; a controller that refuses its part is a warning (`setState` / `loadPreset` warning). |
| X8 | `safePresetName` (device names, 120 bytes, whole UTF-8 characters) also for the vendor / plugin folders; errors carry the system's reason. Tests. (Windows behaviour of device names unverified.) |
| X9 | < / > read the list again when it is older than 3 s; listing reports `moreFiles` and `searchCut` with their own notes. Test. |
| X10 | The Save window remembers the instrument's serial and closes when it changes; folders made at open, the file and its existence only when the name changes. |
| X11 | Stepping starts at the marked entry, also the plugin's current program. GUI: fresh synth (Medium), > → Loud. |
| X12 | The size byte fixed and the offset case added; SDK compatibility test (the SDK's `vstpresetfile.cpp` and its base library compiled into the test program only, without our warnings); arithmetic and stepping tests. |
| X13 | Comment moved back to `trackTableExtra`; a wrong-typed `preset` has its own warning (test); SPEC-VST-06a names the preset. |

Spec v0.44, Plan PL2a. Linux 191/191, Wine 190/190 Catch2 cases (3141 assertions); GUI on Xvfb.

# Crosscheck #17 — 2026-11-09 (after P20–P22: one instance, timeline rename / notes / delete, notes while recording, project note)

Scope: Spec v0.47, Plan, Tests, README, CLAUDE.md, code at `3121a4a` (changes since `888b68d`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Claude checked at the code: Y1 (`path("/x/takes/") == path("/x/takes")` is false with libstdc++, checked with a small program), Y2 (`openProject` sets the note but not the open window's text), Y3 (`main.cpp:4530`: Ctrl+R acts while a text field has the keyboard), Y4 (`main.cpp:3040`: the load result is ignored before Save). CI run 42652851158 green.

| Area | Verdict |
|------|---------|
| Instance lock | ✅ Linux flock / Windows LockFileEx on a byte past the text, PID readable, released at exit and by the system after a crash; checked before the log; show requests coalesce, no half-read shows; ⚠️ tests one process only **Y11** |
| Delete | ✅ guards (recording, auto, playing, render), tracks lose their take, `deleted/` out of the listing; ❌ a takes folder written with a separator at the end makes every Delete fail **Y1** |
| recording.json | ✅ writers all on the GUI thread, time bases match the files (auto pre-roll, MIDI-only, overdub); ⚠️ Save overwrites an unreadable / newer file **Y4**; Save can make a folder that is gone **Y9**; reading cost **Y10**; fsync per mark **Y8** |
| Notes while recording | ⚠️ a half-typed note is lost when the recording ends, also by Ctrl+R typed in the field **Y3**; no ticks on a live MIDI-only take **Y5**; line width / Enter on an empty field **Y7** |
| Project note | ✅ saved, cleared, kept by Save as; ⚠️ an open note window carries the old project's text into a new one **Y2** |
| Names | ⚠️ `##` in a name cuts the label; very long names make huge blocks **Y6** |
| Spec ↔ code ↔ docs | ⚠️ **Y12**; CLAUDE.md's `src/core/` list is out of date |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| Y1 | medium | Delete compares the recording's parent folder with the takes folder as given in the config: written with a separator at the end (`D:\Takes\`, `/home/u/takes/`, hand-edited), they never match and every Delete says "is not a recording in …". | Compare normalised paths without a trailing separator (also when the takes folder is read from the config); test with a trailing separator. |
| Y2 | medium | Opening or making another project while the Project note window is open keeps the old project's text in it; Save writes it into the new project. Clicking the menu or the button again also throws away text being typed. | Close the window when the project changes; open it with the project's note only when it is not open yet. |
| Y3 | medium | A note being typed is dropped without a word when the recording or the part ends; Ctrl+R typed while the field has the keyboard stops the recording (and drops the note). | A note being typed is stored when its recording ends (it belongs to the moment typing began), with a line saying so; Ctrl+R stays the Record key also while typing (SPEC-UI-13), so the note is stored first. |
| Y4 | medium-low | Save in the Name and notes window rewrites a `recording.json` that could not be read (damaged, or from a newer mini-daw), losing its marks and unknown fields; Add note refuses in that case. | Refuse with the reason, and offer "Replace the unreadable file" as a separate button. |
| Y5 | low-medium | A MIDI-only take being recorded has no file yet; the timeline adds it without reading its notes, so marks made meanwhile show no ticks until Stop. | Read the notes when the live recording is added (and again after a mark). |
| Y6 | low | A name with `##` is cut at it on the block (ImGui's ID separator); names have no length limit and the block grows with the name. | Show the name as drawn text (no ImGui label parsing) or replace `##`; cap the shown name at 40 characters with "…" (full name in the tooltip and details). |
| Y7 | low | The transport area (7 lines) needs 9 during an auto part with the note line; the "saved" text is not wrapped (a long note makes a sideways scrollbar); Enter on an empty field leaves the field. | One line more; the result line wrapped / shortened; keep the focus after Enter also when empty. |
| Y8 | low | Each note forces the small file to disk (`fsync`) on the GUI thread while the recorder writes; on a slow disk the window may stall briefly. | Write marks without the forced sync (the rename keeps the file whole); keep the sync for Save. |
| Y9 | low | Save in the Name and notes window makes the folder again if the recording was moved away meanwhile (a folder with only `recording.json`). | Save only into a folder that is there (error otherwise). |
| Y10 | low | Every rescan reads every `recording.json`, and every mark asks for a rescan. | Reuse the notes when the file's size and time are unchanged; after a mark only the live recording is read again. |
| Y11 | tests | Instance lock only tested with two handles in one process; no test of the failed path, a missing folder for requestShow, an empty PID file. Delete: no trailing separator, no `-2` under `deleted/auto`. Marks: invalid UTF-8, equal times. | Add them (a second process: run the test program itself with a hidden option). |
| Y12 | docs | SPEC-PLAY-08 "not while playing" is the player's playing state only (a MIDI-only preview?); nothing about unreadable files (Y4) or long names (Y6); CLAUDE.md's list of `src/core/` files is out of date (no `instance_lock`, `recording_notes`, `presets`, `timeline`, `project`, `instrument`, …). | Spec after the fixes; CLAUDE.md list. |

## Actions taken (user: "yes fix Y1–Y12, Y1 first")
| # | Done |
|---|------|
| Y1 | `withoutTrailingSeparator`: the takes folder and the recording are compared normalised, without a separator at the end. Test with `dir / ""`. |
| Y2 | Opening or making a project closes the note window; the menu and the button keep text being typed. |
| Y3 | `storeUnsentNote` each frame: a note still being typed when its recording ends is saved at its moment, with a line under the transport. GUI: "great solo" typed, Ctrl+R in the field → saved at 2.3 s. Ctrl+R stays the Record key. |
| Y4 | Save refuses an unreadable file with the reason; "Replace the unreadable file" does it on request. |
| Y5 | A live recording added to the timeline reads its notes (MIDI-only takes show their marks). |
| Y6 | `blockName`: at most 40 characters plus "...", "##" shown as "# #"; the block's width from that. |
| Y7 | The transport area 8 lines; the "saved" line wrapped on its own line; focus kept after an empty Enter. |
| Y8 | `writeFileReplacing(…, sync)`: marks written without the forced sync; Save keeps it. |
| Y9 | `saveRecordingNotes` refuses a folder that is not there. Test. |
| Y10 | The notes are read again only when `recording.json`'s size or time changed (`notesStamp`). |
| Y11 | Tests: lock from another process (Linux fork), read-only folder, empty owner file, missing folder; Delete with a trailing separator and "-2" under `deleted/auto`; notes into a gone folder; non-UTF-8 note text. |
| Y12 | SPEC-PLAY-08 (playing includes a recording played from the timeline — a preview always starts the player —, unreadable files, names, trailing separator, cache), SPEC-REC-09, SPEC-PRJ-03; CLAUDE.md `src/core/` list. |

Spec v0.48, Plan P22a. Linux 200/200, Wine 198/198 Catch2 cases (3258 assertions); GUI on Xvfb (Y3).

---

# Crosscheck #18 (after P23 and P24)

Scope: Spec v0.50, Plan, Tests, README, CLAUDE.md, code at `6301676` (changes since `6590221`: P23 Terms tab `c89fec7`, P24 Linux plugin editor `6301676`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Claude checked each finding at the code before listing it. CI run 42707274316 green (Linux 207/207 under `xvfb-run`, T-VST-39 ran there; Windows 205/205).

| Area | Verdict |
|------|---------|
| Run loop (core) | ✅ readers before timers, a late timer once, callbacks copied and entries found again by id, so a callback can remove itself or others; ❌ a reader whose descriptor was closed (POLLNVAL) is called every frame, and after the number is reused it is told about another file **Z1**; ❌ handler objects can be destroyed inside the run loop's own erase, and their destructors may unregister (re-entry) **Z2** |
| IRunLoop on the frame | ✅ only on Linux, handlers held while registered, all of an editor's dropped at close and on unload before the module goes; ⚠️ unregister matches a pointer of either kind **Z5** |
| Linux editor window | ✅ X11 window made on the server before attach, transient for the main window, sizes in pixels, same close rules as Windows; ⚠️ no check that GLFW runs on X11 (safe while GLFW is built without Wayland) **Z3**; keyboard focus inside the plugin's own window not tested **Z6**; the app side is tested by hand only **Z4** |
| Main loop | ✅ poll after `glfwPollEvents`, outside ImGui frames, also while minimised; not while slow work blocks the thread (as everything else) |
| Terms tab | ✅ texts checked against Spec and code (names, defaults, labels); search as specified; IDs per term; old configs get the tab; ⚠️ "> " mark never cleared **Z7**; a query that matches every term shows the topic view **Z8** |
| Tests / CI | ✅ T-VST-37..39 test what they claim; `xvfb-run` affects only the X11 test; ⚠️ the test synth cannot show Z1 / Z2 **Z4** |
| Spec ↔ Plan ↔ docs | ⚠️ stale "Linux later" in Plan P10d and CLAUDE.md; Spec says hang-up / error descriptors are called (also wrong after Z1) **Z9** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| Z1 | medium | `RunLoop::poll` treats POLLNVAL (descriptor not open) as ready: a plugin that closes its X11 connection before unregistering is called every frame until its editor closes; when the number is reused by another file (audio, MIDI, the log) the plugin is told about a descriptor it does not own. T-VST-37 "hang-up counts as ready" is fine, but nothing covers NVAL. | On POLLNVAL drop the reader (not called) and log it once; keep calling on POLLIN / POLLERR / POLLHUP. Test: close the pipe's read end, poll → not called, reader gone. |
| Z2 | medium | Removing an entry (`remove`, `removeOwner`, and through them the frame's `unregister` / `dropHandlers`) destroys its callback, and with it the last reference to a plugin object, inside `vector::erase`. In `InstrumentEditor::close()` the view's own reference is released first, so `dropHandlers` can run the view's destructor there; a destructor that unregisters (common) re-enters `remove` / the frame's `handlers_` loop while they are being changed → undefined behaviour. | `RunLoop::remove` / `removeOwner`: move the entries out, erase, and destroy them after the list is consistent. Frame: `unregister` takes its ids out of `handlers_` first, then removes them; `dropHandlers` swaps `handlers_` out before `removeOwner`. Test: a callback object whose destructor removes another entry. |
| Z3 | low | `plugin_window.cpp` uses `glfwGetX11Display()` / `glfwGetX11Window()` without a check. GLFW is built X11-only now, but with Wayland enabled (P12) they return null and `XSetTransientForHint` / `XSync` crash. | `supported()` also needs `glfwGetPlatform() == GLFW_PLATFORM_X11`; in `open()` a null display gives an error ("needs X11 / XWayland"). |
| Z4 | low | Test gaps: the app's Linux window code (`plugin_window.cpp`) is tested by hand only (T-VST-40); the test synth always unregisters in `removed()` and never closes its connection first, so it cannot show Z1 / Z2; registering from `setFrame()` is not tested. | Cover Z1 / Z2 in `test_run_loop.cpp` (closed descriptor, destructor that unregisters); test-synth hidden switch to register in `setFrame()` and to close its X11 connection before unregistering (T-VST-38). The app window stays a manual test (T-VST-40). |
| Z5 | low | The frame keeps readers and timers in one list keyed by the handler pointer: `unregisterTimer(p)` would also remove readers registered with the same pointer, and the other way round (only possible with odd plugins). | Keep the kind (reader / timer) in each record and match it. |
| Z6 | low | Keyboard input in a plugin's own window (text fields, a plugin's keyboard) is not tested; mini-daw focuses its own X11 window, the plugin's child window has to take the focus itself. | No code change now; add a step to T-VST-40 (type in a plugin's text field) and look at it with a real plugin. |
| Z7 | low | Terms tab: the "> " mark of a term jumped to stays for the rest of the session, also in later search results. | Clear it when the Find text changes. |
| Z8 | low | Terms tab: a query that matches every term (e.g. "e") shows the topic view without the topics per term and "47 terms", while the Spec says found terms are shown name-first with their topic; the "no term has" text shows the untrimmed query. | Choose the view by "Find is empty (after trimming)"; show the trimmed query. |
| Z9 | doc | Plan P10d still says "Linux later … Linux editor later (P12)"; CLAUDE.md line 3 "Windows 10 (Linux/macOS later)"; SPEC-VST-05b "ready descriptors (also on error or hang-up) are called" needs Z1's rule. | Update the texts. |

## Actions taken (user, 2026-11-10: "Shall I fix Z1–Z9, starting with Z2: yes")

| # | Action |
|---|--------|
| Z2 | `RunLoop::remove` / `removeOwner` move the entries out and release them after the list is consistent; the frame's `unregister` takes its records out of `handlers_` before removing them, `dropHandlers` empties `handlers_` first. Test with handler objects whose destructor removes other entries; the old code fails it. Test synth mode 3 (unregisters only in the view's destructor): close and unload clean, also under AddressSanitizer / UBSan. |
| Z1 | A reader whose descriptor is not open (POLLNVAL) is removed without a call and counted (`droppedCount`); the app logs a warning once per drop. Tests: a closed pipe in T-VST-37; test synth mode 2 (closes its pipe without unregistering) in T-VST-38. |
| Z3 | `PluginWindows::supported()` on Linux also needs `glfwGetPlatform() == GLFW_PLATFORM_X11`; `open()` reports "needs X11 (or XWayland)" for a missing display or window. |
| Z4 | Test synth hidden parameter "Editor test mode" (7): 1 register in `setFrame()`, 2 close the pipe without unregistering, 3 unregister from the destructor; T-VST-38 sections for each. The app's X11 window stays a manual test (T-VST-40). |
| Z5 | The frame's records keep the kind (reader / timer); `unregisterTimer` / `unregisterEventHandler` match it. (Not separately testable with the test synth: its two interfaces have different addresses.) |
| Z6 | T-VST-40: type in a plugin's text field. No code change. |
| Z7 | The "> " mark is cleared when Find is edited and by Show all. |
| Z8 | The topic view only for an empty Find after trimming (`trimmedTermQuery`); any word gives the result list ("47 terms" when all match); "No term has …" shows the trimmed word. |
| Z9 | Plan P10d, CLAUDE.md line 3 and SPEC-VST-05b (closed descriptors, release order, kinds, X11 only) updated; SPEC-UI-17 for Z7 / Z8. |

Spec v0.51, Plan P24a. Linux 209/209 ctest under `xvfb-run`; MinGW build under Wine 204/204 Catch2 cases (3809 assertions). The Terms tab changes (Z7, Z8) not checked in the GUI here: part of T-UI-28 (M).
