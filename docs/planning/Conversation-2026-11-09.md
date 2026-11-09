# Conversation log 2026-11-09

Continues `Conversation-2026-11-08.md` (PL1, crosscheck #15, PL2).

## 1. CI of `f8cf624`, crosscheck #16
> pushed it, check the CI run and do the crosscheck
- Run 42532989769: Linux 187/187, licence green, **Windows 187/188**: T-VST-35 failed — the test made `café.vstpreset` from a narrow string literal, which MSVC converts with the ANSI code page (a test defect; mini-daw itself converts with `pathFromUtf8`). No new build in `dist/`; it still holds run 42529973283 (`8008ffc`). (Copying the new build had emptied `dist/` before the Windows download turned out missing; the previous build was put back from the scratchpad. From now on the downloads are checked before `dist/` is emptied.)
- Crosscheck #16 (`Crosscheck-2026-11-09.md`; Spec v0.43, code at `f8cf624`, independent reviewer): X0 the Windows test failure; X1 a preset's state is set while the audio thread processes the instrument; X2–X11 small (same label twice, saved label, Enter in Save, program-list variants, recording rule, controller results / hex check, device names, cache, Save window, stepping start); X12 tests; X13 docs.

## Handover (state at the end of 2026-11-09)
> tomorrow
- Code: PL2 (`f8cf624`) is the latest code; Spec v0.43, Plan up to PL2. CI run 42532989769 is red on Windows only by a test defect (X0: a non-UTF-8 file name made in T-VST-35); Linux green.
- Next: fix crosscheck #16 X0–X13 (proposed: X0 first, then X1). Not yet confirmed by the user.
- `dist/`: still run 42529973283 (`8008ffc`, PL1 + crosscheck #15 fixes) with its `TESTS.txt`; refresh it after a green run with the PL2 fixes (add T-VST-36: what Arturia offers in the Presets menu / log line).
- Waiting for the user (Windows): the tests in `dist/TESTS.txt` (T-UI-25..27 PL1, T-UI-23, -22, -20, -18, -17 / T-PLAY-18, T-REC-34, T-VST-30), later T-VST-36.

## 2. Where are we; crosscheck #16 fixes (PL2a)
> where are we ?
- Summary given: PL1 and PL2 done, crosscheck #16 open (X0–X13), CI red on Windows by X0, `dist/` at `8008ffc`.
> yes fix X0–X13, X0 first
- X0–X13 as proposed (see `Crosscheck-2026-11-09.md`, actions). The SDK compatibility test needs the SDK's `vstpresetfile.cpp` and its base library in the test program (the same files the test synth builds), compiled without our warnings.
- Spec v0.44 (SPEC-VST-07a, -06a), Plan PL2a, T-VST-33..36 extended, T-VST-15 (preset name). Linux 191/191, Wine 190/190; GUI on Xvfb.

## 3. CI of `2730bf9`
> pushed it, check the CI run / commit the CI log lines (when CI is done)
- Run 42626012113 green (Linux 191/191, Windows 192/192: X0 is fixed on MSVC). Both builds downloaded and checked first, then put in `dist/`; `dist/TESTS.txt` has T-VST-36 (presets; the log line that shows what Arturia offers) on top of the PL1 tests still open.

## 4. One mini-daw per user (P20); timeline right-click and notes planned (P21, P22)
> can we block the mini-daw from starting 2 times for the same user, it makes no sesns i think. in the timeline tab when we right click on one of the takes can we add a delete function and a rename. I would also allow the user to make annotations per take and for the while recording would that be possible ?
- Read "sesns" as "sense"; asked what "for the while recording" meant, and three design questions. Answers: notes both per take, while recording (at their moment) and for the whole project; Delete moves the take to a `deleted` folder; Rename is a name shown, the folder keeps its date and time; a second start brings the first window up.
- Plan: P20 (one instance), P21 (timeline: Rename, Notes, Delete), P22 (notes while recording, project note).
- P20 done: `InstanceLock` (OS file lock in the config folder, owner's process ID, show request polled twice a second), checked before the log is opened; command-line runs not blocked. SPEC-RUN-01, T-RUN-01/02. Linux 193/193, Wine 192/192; second start under Xvfb and Wine.
> when in the plan will we bw able to use the linux vst3 edit function on a track?
- Answer: it is part of P12 (full Linux support, "later"); it needs a VST3 run loop (IRunLoop) and an X11 window to embed the editor, a step like P10d. Offered to schedule it after P20–P23 or earlier; not decided yet.
> we should add a terminology tab in the notebook explaining the keywords likel app, recording track, take instrument and so on
- Read "likel" as "like". Planned as P23: a Terms tab (in the tab order like the others), after P21 and P22 unless the user wants it sooner.

## 5. CI of `b5f96c2`
> pushed it, check the CI run / commit the CI log lines
- Run 42636015090 green (Linux 193/193, Windows 194/194); both builds checked, then in `dist/`; `dist/TESTS.txt` has T-RUN-02 (starting twice) on top, then the PL2 and PL1 tests still open.

## 6. P21: rename, notes and delete on the timeline
> yes do P21, linux editor after P23
- Plan: P24 (the Linux plugin editor) after P23.
- Core: `recording_notes` (name, note, marks in `recording.json` in the recording's folder; `moveRecordingToDeleted` to `deleted/[auto/]`); the timeline reads the notes with each recording; a block can be wider for its name.
- GUI: right-click menu on a block (Rename, Notes, Delete), Name and notes window, Delete question naming the tracks that lose their take; Delete not while recording, playing or rendering. Found in the GUI check: the menu closed at once (its ID came from the label, which changes when selected), and a named block cut its label; both fixed.
- Spec v0.46 (SPEC-PLAY-08), T-PLAY-19/20, README. Linux 196/196, Wine 195/195; GUI on Xvfb.

## 7. CI of `e2ad215`
> pushed it, check the CI run / commit the CI log lines
- Run 42647627344 green (Linux 196/196, Windows 197/197); both builds checked, then in `dist/`; `dist/TESTS.txt` has T-PLAY-20 (rename, notes, delete) on top, then T-RUN-02 and the PL2 / PL1 tests still open.

## 8. P22: notes while recording, project note
> yes do P22
- Core: `addRecordingMark` (in time order, folder made if needed, a damaged file not overwritten); `ProjectData::note` (`"note"` in project.json).
- GUI: "Note at mm:ss.t: [ ] Add note" in the transport area while a take or an auto part is recorded; ticks on the timeline blocks with tooltips; marks in the details and with Remove in the Name and notes window; File → Project note… and a button and line above the timeline.
- Found in the GUI check: the note was stored at the moment of Enter, not when typing began (now the first key's moment, the time shown freezes while typing); the tick lines ran through the label (now the top third).
- Spec v0.47 (SPEC-REC-09, SPEC-PRJ-03), T-REC-35, T-PRJ-09, README. Linux 198/198, Wine 197/197; GUI on Xvfb.

## 9. CI of `968ef45`
> pushed it, check the CI run / commit the CI log lines (when CI is done)
- Run 42652851158 green (Linux 198/198, Windows 199/199); both builds checked, then in `dist/`; `dist/TESTS.txt` has T-REC-35 and T-PRJ-09 on top, then the P21, P20, PL2 and PL1 tests still open.

## 10. Crosscheck #17 (after P20–P22)
> pushed it, do the crosscheck first
- Crosscheck #17 in `Crosscheck-2026-11-09.md` (Spec v0.47, code at `3121a4a`, independent reviewer): no high finding. Y1 Delete fails when the takes folder is written with a separator at the end; Y2 the project note window carries text into another project; Y3 a half-typed note is lost when the recording ends (also by Ctrl+R); Y4 Save rewrites an unreadable recording.json; Y5–Y10 small (live MIDI-only ticks, `##` / long names, transport lines, fsync per mark, a folder made again, reading cost); Y11 tests; Y12 docs.

## 11. Crosscheck #17 fixes (P22a)
> pushed it, yes fix Y1–Y12, Y1 first
- Y1–Y12 as proposed (see `Crosscheck-2026-11-09.md`, actions). Spec v0.48, Plan P22a, CLAUDE.md `src/core/` list. Linux 200/200, Wine 198/198; GUI on Xvfb.

## 12. CI of `7837f17`
> pushed it, check the CI run
- Run 42664805653 green (Linux 200/200, Windows 200/200); both builds checked, then in `dist/`; `dist/TESTS.txt` has steps for the fixes (a note saved at Ctrl+R, MIDI-only ticks, the note window closing with another project).

## 13. P23: the Terms tab

User: "pushed it, yes do P23" (`75bf825` pushed).

- New core module `terms` (`terms.hpp/.cpp`): 47 terms in 6 topics (Basics, Recording, Playing, Instruments, Projects and files, Sound and audio setup), each with what it means, where it is in mini-daw and See also; `findTerms` (any case, name matches first; also the Where text) and `termIndex`.
- Tab id `terms` added to the tab order (`defaultTabOrder`, `isTabId`, config text and warnings); an older `order` gets it at the end without a warning.
- GUI (`main.cpp`): `drawTermsTab` / `drawTerm`: Find field, Show all, "N of M terms", the list in a scrolling child; See also buttons wrap and jump to the term ("> " mark).
- Tests: `test_terms.cpp` (T-UI-28, 3 cases), `test_config.cpp` updated for four tabs. Linux 203/203; MinGW build under Wine 201/201; GUI checked on Xvfb.
- Spec v0.49: SPEC-UI-17; SPEC-UI-11 / -12 name the Terms tab. Plan P23 done.

## 14. CI of `c89fec7`

User: "pushed it, check the CI run". Run 42694188393: windows, linux, licence green; Linux 203/203, Windows 203/203. Both builds downloaded and checked (the Terms texts are in them), then `dist/` refilled; `dist/TESTS.txt` has T-UI-28 at the top.
