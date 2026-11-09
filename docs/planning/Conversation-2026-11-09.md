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
