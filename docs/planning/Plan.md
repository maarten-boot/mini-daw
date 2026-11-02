# Plan — mini-daw

## Status
| # | Step | Spec items | State |
|---|------|-----------|-------|
| P0 | Project setup: git repo, `docs/planning` branch, planning docs | — | done (2026-11-01) |
| P1 | Turn the scenario into Spec items | all | done (Spec v0.4, defaults confirmed) |
| P1b | Licence: MIT `LICENSE` on `main`, licence rules in Spec | LIC-01..04 | done (2026-11-01) |
| P2 | Project skeleton: CMake, dependencies (RtAudio, RtMidi, GLFW, Dear ImGui), `THIRD_PARTY.md`, licence check in CI, CI build on Windows | TECH-01..03, 05, 06, LIC-02, CI-01..03 | done (2026-11-01); CI on GitHub green since run 41337681818 |
| P3 | Theme/contrast system, including WCAG ratio calculation, presets and config file | UI-01..06 (UI-07 with meters in P7) | done (2026-11-01) |
| P4 | Device layer: list devices, select them, save settings; text-size setting | DEV-01..06, UI-08 | done (2026-11-01); audio on real Windows not yet tested |
| P5 | Audio engine: lock-free callback, ring buffers, disk writer thread, WAV writer; `--record-test` | NF-01, REC-03, REC-05/05a–c, REC-06 | done (2026-11-01); crosscheck #3 fixes (silence for lost audio, disk sync option, loss counting on disk errors). Recording on real Windows not yet tested; Record button comes with the tracks in P6 |
| P6 | Tracks and multi-track recording (audio + MIDI), SMF writer, sample-accurate MIDI timestamps | REC-01..04, 01a, 02a, 04a–c, 07 | P6a, P6b done (2026-11-02); P6c (real MIDI) pending |
| P6a | Core: SMF writer/reader, MIDI take builder, clock→frame mapping, MIDI inputs, recording session, `[recording]` settings | REC-04a–c, 07 | done (2026-11-02) |
| P6b | GUI: track list, Arm, Record/Stop, Settings → Recording…; `--record-test` through the session (audio + MIDI) | REC-01a, 02a, 06 | done (2026-11-02) |
| P6c | Manual tests with real MIDI: Linux VM (virtual keyboard, `audio` group), Windows (USB keyboard, virtual port) | REC-01, 04 | pending |
| P7 | Monitoring and meters | MON-01..02, UI-07 | pending |
| P8 | Transport and playback | PLAY-01..04 | pending |
| P9 | Project save/load | PRJ-01..02 | pending |
| P9b | Windows packaging: GUI subsystem (no console window; `--list-devices` still prints), icon, installer | — | pending |
| — | **Milestone v1: record and play back MIDI and audio on Windows 10** | | |
| P10 | VST3 hosting: scan, load, process, editor window, save state | VST-01, 02, 05, 06 | pending |
| P11 | Record VST3 output live, render MIDI through VST3 | VST-03, 04, NF-03 | pending |
| — | **Milestone v2: VST3 instruments** | | |
| P11b | Optional ASIO backend (`MINIDAW_WITH_ASIO`, off by default) | TECH-02, LIC-03/04 | later |
| P12 | Full Linux support (JACK/PipeWire, native Wayland) and macOS (CoreAudio); Linux already builds and runs in CI | TECH-05 | later |
| P13 | Note and sound editing | — | later (not in v1) |
| PX | Crosscheck Spec ↔ Plan ↔ code | — | recurring. #1 after P2, #2 after P4, #3 after P5 (all in `Crosscheck-2026-11-01.md`) |

## Notes
- Every new Spec item gets at least one test case in `Tests.md`.
- Pure logic (contrast maths, WAV/SMF writers, ring buffer, timeline) gets automated unit tests (Catch2). Device and plugin behaviour gets manual test cases.
- Design for later editing: takes are immutable files, and tracks refer to them through clips (offset and length). This leaves room for non-destructive editing later.
