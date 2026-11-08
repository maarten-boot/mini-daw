# Conversation log 2026-11-08

## 1. A test list with every test build
> ok each time you write a program for me to run in dist also write me a small text file with the things that should be tested in this cycle.
- `dist/TESTS.txt` written for the build in `dist/` (CI run 42421955192, `0309fed`): new in this build (T-UI-23, T-UI-22, T-UI-20, T-UI-18), still open from earlier builds (T-UI-17 / T-PLAY-18, T-REC-34, T-VST-30) and general checks, each with steps and the expected result.
- From now on written with every build put in `dist/` (CLAUDE.md, memory).

## 2. Review of the docs; tidy-up
> read the current dir structire *.md and all under docs then report
- Read "structire" as "structure". Report: layout, root docs, Spec v0.39, Plan (P0–P19a done), Tests (150 cases), crosschecks #1–#14, the logs, Demo-checklist, open Windows tests; eight remarks.
> this a project i own, no need for planning to live in a separate branch. yes fix 2–8
- Planning docs stay on `main` (CLAUDE.md, memory); the old `docs-planning` branch is not used any more.
- Handover section added below (the latest log had none).
- `Crosscheck-2026-11-02.md` (it holds #4–#14, 2026-11-02 to 11-07) renamed `Crosscheck-2026-11-02-to-11-07.md`; references in Plan and the logs updated.
- Tests.md Results: the last rows of 2026-11-07 put in the order they happened (P17 CI, #12 fixes, their CI, P18, P18 CI, the user's Windows test, the text size fix, …). No row changed.
- Demo-checklist: the build is now CI run 42421955192 (`0309fed`), the one in `dist/`.
- CLAUDE.md: the two pinning lines merged into one.
- Spec v0.40: items put in number order (REC-06/07, VST-03a..05a, UI-07 / UI-02a / UI-13 / UI-14, LIC-04/05, CI-03/04); no text changed.
- `.gitignore`: `*.xmp` (image editor sidecar files in `res/`).

## Handover (state at the end of 2026-11-08)
- Code: P19a (`0309fed`) is the latest; CI run 42421955192 green (Linux 173/173, Windows 174/174). Spec v0.40, Plan up to P19a, crosscheck #14 done and fixed.
- `dist/`: Windows + Linux builds of run 42421955192 with `dist/TESTS.txt`.
- Waiting for the user (Windows): T-UI-23, T-UI-22, T-UI-20, T-UI-18 (new), T-UI-17 / T-PLAY-18, T-REC-34, T-VST-30, plugin editors / scaling / text size with ImGui 1.93 and GLFW 3.5.1; older: T-PLAY-12 (overdub loopback), T-NF-02, T-DEV-07.
- Next steps (not started): results of those tests; later PL1 (hover / selected look), PL2 (preset menu), P12 (full Linux / macOS), P14 (ASIO), P13 (editing).

## 3. CI of `31658df`
> pushed it, check the CI run
- Run 42518027178 green (windows, linux, licence). Docs only: `dist/` keeps the build of run 42421955192. The handover above still holds.

## 4. PL1: look and feel
> start PL1
- Looked at the GUI first (Xvfb): hover and pressed differ little from normal (1.2:1, 1.5:1) and not at all at Max; disabled controls were faded by ImGui to 60 % at every setting, below the minimum at AAA and Max (a contrast bug the check did not see); the MIDI port list sized itself from the space left, so the Take column and X were cut off; slow clicks (instrument, Apply, projects) freeze the window without a sign.
- Asked four questions; the user chose all the recommended options: frames + colours; flat disabled controls faded within the minimum; an Output column + sideways scrollbar; a busy message.
- Core: `minimumOpacity`, `disabledPairs`, `disabledOpacity` (AA 89 %, AAA and above 100 %), Accent pairs on every control / list background, pressed #A6A6A6, hovered #CCCCCC, Accent #00336F. `--contrast-report` lists the disabled pairs as drawn.
- GUI (`src/app/state_cues`): ImGui's item hooks (`IMGUI_ENABLE_TEST_ENGINE`, hooks only) find the hovered / pressed / disabled controls; an Accent frame is drawn round the hovered one; disabled ones are flat with a dashed frame (flat alone is invisible at Max); `choice()` gives the current list entry "> " and a bar; the selected tab gets a thick line (`DrawSelectedOverline`). Track table: Output column, fixed widths, scrollbar, first four columns frozen. `startBusy()` for instrument, Apply, open / new / save-as project.
- Spec v0.41 (SPEC-UI-02a, -10, -15, -16, PLAY-03a, §8 Max), Plan PL1 done, T-UI-24..27, README, Demo-checklist, CLAUDE.md. Linux 177/177, Wine 176/176; GUI on Xvfb and Wine.

## 5. CI of `7188ddd`, crosscheck #15
> pushed it, check the CI run and do the crosscheck
- Run 42527512800 green (Linux 177/177, Windows 178/178); both builds in `dist/` with a new `dist/TESTS.txt` (T-UI-25..27 new, the earlier open tests).
- Crosscheck #15 (`Crosscheck-2026-11-08.md`; Spec v0.41, code at `7188ddd`, independent reviewer as in #12–#14): no high finding. W1 ImGui's own disabled menu items have no cue (at Max they look enabled); W2 pairs drawn but not checked (checked disabled checkbox, disabled selected entry, frames on title bar / scrollbar); W3–W8 small (one-frame lag, dropped busy request, busy text wrap, choice alignment, dash cost / stray endDisabled, table height); W9 tests; W10–W11 docs.

## 6. Crosscheck #15 fixes (PL1a)
> yes fix W1–W11, W1 first
- W1–W11 as proposed (see `Crosscheck-2026-11-08.md`, actions). W6 turned out not to be a defect (monospaced font). The AA fade of disabled controls is now 95 % (the Selection pairs added in W2 need it).
- New headless ImGui test of the cues (`tests/test_state_cues.cpp`; the test program now links Dear ImGui when the GUI is built).
- Spec v0.42, Plan PL1a, T-UI-24 / T-UI-25 extended. Linux 182/182, Wine 181/181; GUI on Xvfb at Max.
