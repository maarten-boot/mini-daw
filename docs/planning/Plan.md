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
| P6c | Manual tests with real MIDI: Linux VM (virtual keyboard, `audio` group), Windows (USB keyboard, virtual port) | REC-01, 04 | Linux done (2026-11-02, Midi Through + aplaymidi); Windows pending |
| P7 | Monitoring and meters | MON-01..02, 01a, 02a, UI-07 | done (2026-11-02) on Linux; Windows (WASAPI) to be tested by the user |
| P7b | MIDI hot-plug and diagnostics log (from the user's Windows test) | REC-01b, LOG-01 | done (2026-11-02) on Linux and Wine; Windows with a USB keyboard to be tested by the user |
| P8 | Transport and playback: streamed audio playback, MIDI playback to outputs, volume/mute/solo, overdub with latency estimate, open take folder, stream watchdog | PLAY-01..06, 01a..04a, DEV-05 | done (2026-11-02) on Linux; Windows (WASAPI, MIDI out) and the overdub loopback measurement to be tested by the user |
| P9 | Projects: project folder with project.json and takes, autosave, open/new/save as/recent, reopen last, takes copied in | PRJ-01..02, 01a, 02a | done (2026-11-02) on Linux and Wine; Windows by the user |
| P9b | Windows packaging: mini-daw.exe without console + mini-daw.com for the command line, icon, NSIS installer (per user), built in CI | PKG-01..03 | done (2026-11-03) on Linux/Wine; real Windows (installer pages, SmartScreen, Start menu) by the user |
| — | **Milestone v1: record and play back MIDI and audio on Windows 10** | | |
| P10 | VST3 hosting: scan, load, process, editor window, save state | VST-01, 02, 05, 06 | done on Windows (P10a–c, e 2026-11-03; P10d 2026-11-04); the editor on Linux later |
| P10a | SDK 3.8.1 (hosting part) as a dependency; scanning with cache, moduleinfo.json, crash marker | VST-01, 01a | done (2026-11-03) |
| P10b | Instrument (load, process, MIDI mapping, parameters, state) and the instrument rack in the audio engine (live MIDI taps, playback MIDI at the sample); GUI: Settings → Instruments, instrument per MIDI track | VST-02, 02a | done (2026-11-03) on Linux and Wine; real plugins by the user |
| P10c | Test synth (a VST3 instrument built with the tests) | — | done (2026-11-03) |
| P10d | Plugin editor window (Windows first, Linux later): an "Edit" button per instrument track opens the plugin's own window (presets, sounds), which can stay open while playing and be closed again | VST-05, 05a | done (2026-11-04) on Linux/Wine (tests, live under Wine); real plugins on Windows by the user; Linux editor later (P12) |
| P10e | Instrument and its state in the project | VST-06, 06a | done (2026-11-03) |
| P11 | Record VST3 output live, render MIDI through VST3 | VST-03, 04, NF-03 | done (2026-11-05) on Linux/Wine; Windows by the user |
| P11a | Linked sound track per instrument track, "Sound from" switch (instrument / this recording), kept in the project | VST-03a | done (2026-11-05) |
| P11b | Live recording of the instrument's sound (Recorder takes from the rack, also without an input device) | VST-03, 03a | done (2026-11-05) |
| P11c | Offline render of a MIDI take through a second instance, progress and Cancel | VST-04, 04a | done (2026-11-05) |
| P11d | After the user's Windows test: linked sound track made when the instrument is chosen (removed again while empty); overdub hint and Arm tooltip | VST-03a, PLAY-05a | done (2026-11-05) |
| — | **Milestone v2: VST3 instruments** | | |
| P14 | Optional ASIO backend (`MINIDAW_WITH_ASIO`, off by default; was "P11b" until 2026-11-05, renamed when P11 got its own a–c) | TECH-02, LIC-03/04 | later |
| P12 | Full Linux support (JACK/PipeWire, native Wayland) and macOS (CoreAudio); Linux already builds and runs in CI | TECH-05 | later |
| P13 | Note and sound editing | — | later (not in v1) |
| P15 | Main window reopens at its last size and place, maximised if it was; `[window]` in the config (user request 2026-11-06) | UI-09 | done (2026-11-06); Windows check by the user (T-UI-15) |
| P15b | After the user's Windows test: start without an empty or half-sized window (hidden until the first frame, maximised in one step); fixed-height transport area so the table does not jump | UI-09, UI-10 | done (2026-11-06) |
| P16 | Auto record: parts start at your playing and end after a pause, saved with date and time; tracks unchanged. Recorder parts mode (memory of the last 4 s, parts opened/closed by the disk thread), AutoPartTracker, session auto mode, GUI button and status, settings | REC-08 | done (2026-11-06) on Linux/Wine, crosscheck #11 fixes S1–S12; Windows by the user |
| P17 | Tabs (Recording / Timeline) and the timeline of recordings: auto parts and takes, long pauses short, play a recording, load it onto the tracks | UI-11, PLAY-07 | done (2026-11-07) on Linux/Wine; Windows by the user |
| P17a | Crosscheck #12 fixes (T1–T14); fetched libraries pinned by commit hash and updated (RtMidi 6.1.0, ImGui v1.93.0, GLFW 3.5.1) | PLAY-07, UI-11, CI-04 | done (2026-11-07) on Linux/Wine; Windows by the user |
| P18 | Audio setup tab (audio stream and devices; a status line stays on the Recording tab); tab order by dragging or Settings → Tabs…, saved with the tab used last | UI-11, UI-12 | done (2026-11-07) on Linux; Windows by the user |
| P18a | Crosscheck #13 fixes (U1–U8): keyboard navigation in every window (Space / Home stay the transport keys); the last tab at start without another tab first; Tabs dialog focus; audio notes counted and limited | UI-12, UI-13 | done (2026-11-07) on Linux/Wine; Windows by the user |
| P19 | Browse… buttons for every folder field (Windows: the system folder dialog; Linux: mini-daw's own browser); instrument folders checked when added, Remove button in front | UI-14, VST-01b | done (2026-11-07) on Linux/Wine; Windows by the user |
| P19a | Crosscheck #14 fixes (V1–V11): any folder name, closing while choosing, no shortcuts behind menus / the browser, large folders, all folder fields cleaned | UI-13, UI-14, VST-01b | done (2026-11-07) on Linux/Wine; Windows by the user |
| PL1 | Look and feel: clearer hover, active and selected states (e.g. the chosen instrument), so every click gives feedback; colours through roles, contrast ≥ the minimum (user feedback 2026-11-03). Also: the Take column gets narrow in a small window, so the output list can be cut off. User's choices 2026-11-08: frames + colours, flat disabled controls faded within the minimum, an Output column + sideways scrollbar, a busy message | UI-02a, UI-10, UI-15, UI-16, PLAY-03a | done (2026-11-08) on Linux/Wine; Windows by the user |
| PL1a | Crosscheck #15 fixes (W1–W11): ImGui's own disabled menu items, title bar / scrollbar, ticked disabled checkbox, Selection pairs, no hover delay, busy guards and wrapping, table height, headless ImGui tests | UI-02a, UI-10, UI-15, UI-16 | done (2026-11-08) on Linux/Wine; Windows by the user |
| PL2 | Preset menu in mini-daw: the plugin's program list (VST3 IUnitInfo), .vstpreset files in the standard folders, Save preset; Presets, < and > in the Output column (user's choices 2026-11-08). Test synth got a program list | VST-07, 07a | done (2026-11-08) on Linux/Wine; Windows with real plugins (what Arturia offers) by the user |
| PX | Crosscheck Spec ↔ Plan ↔ code | — | recurring. #1 after P2, #2 after P4, #3 after P5 (`Crosscheck-2026-11-01.md`), #4 after P6a/b, #5 after P7/P7b, #6 after P8, #7 after P9, #8 after P9b, #9 after P10, #10 after P11, #11 after P16, #12 after P17, #13 after P18, #14 after P19 (`Crosscheck-2026-11-02-to-11-07.md`), #15 after PL1 (`Crosscheck-2026-11-08.md`) |

## Notes
- Every new Spec item gets at least one test case in `Tests.md`.
- Pure logic (contrast maths, WAV/SMF writers, ring buffer, timeline) gets automated unit tests (Catch2). Device and plugin behaviour gets manual test cases.
- Design for later editing: takes are immutable files, and tracks refer to them through clips (offset and length). This leaves room for non-destructive editing later.
