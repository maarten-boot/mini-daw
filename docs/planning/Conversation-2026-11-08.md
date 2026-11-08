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
