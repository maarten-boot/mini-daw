# Plan — mini-daw

## Status
| # | Step | Spec items | State |
|---|------|-----------|-------|
| P0 | Project setup: git repo, `docs/planning` branch, planning docs | — | done (2026-11-01) |
| P1 | Turn the scenario into Spec items | all | done, draft v0.2. Waiting for confirmation of the *(proposed)* defaults |
| P2 | Project skeleton: CMake, dependencies (RtAudio, RtMidi, GLFW, Dear ImGui), CI build on Windows | TECH-01..06 | pending |
| P3 | Theme/contrast system, including WCAG ratio calculation, presets and config file | UI-01..07 | pending |
| P4 | Device layer: list devices, select them, save settings | DEV-01..03 | pending |
| P5 | Audio engine: lock-free callback, ring buffers, disk writer thread, WAV writer | NF-01, REC-03, REC-05 | pending |
| P6 | Tracks and multi-track recording (audio + MIDI), SMF writer, sample-accurate MIDI timestamps | REC-01..04 | pending |
| P7 | Monitoring and meters | MON-01..02, UI-07 | pending |
| P8 | Transport and playback | PLAY-01..04 | pending |
| P9 | Project save/load | PRJ-01..02 | pending |
| — | **Milestone v1: record and play back MIDI and audio on Windows 10** | | |
| P10 | VST3 hosting: scan, load, process, editor window, save state | VST-01, 02, 05, 06 | pending |
| P11 | Record VST3 output live, render MIDI through VST3 | VST-03, 04, NF-03 | pending |
| — | **Milestone v2: VST3 instruments** | | |
| P12 | Port to Linux (ALSA/JACK/PipeWire) and macOS (CoreAudio) | — | later |
| P13 | Note and sound editing | — | later (not in v1) |
| PX | Crosscheck Spec ↔ Plan ↔ code | — | recurring, after significant changes |

## Notes
- Every new Spec item gets at least one test case in `Tests.md`.
- Pure logic (contrast maths, WAV/SMF writers, ring buffer, timeline) gets automated unit tests (GoogleTest or Catch2 *(proposed)*). Device and plugin behaviour gets manual test cases.
- Design for later editing: takes are immutable files, and tracks refer to them through clips (offset and length). This leaves room for non-destructive editing later.
