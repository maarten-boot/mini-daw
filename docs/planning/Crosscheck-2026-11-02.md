# Crosscheck #4 — 2026-11-02 (after P6a/P6b and the MIDI clock smoothing)

Scope: Spec v0.13, Plan, Tests, README, code at commit `c80053f`. Spec ↔ Plan ↔ Tests checked by Claude; the P6 code was also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). The main findings were checked again in the code.

| Area | Verdict |
|------|---------|
| NF-01 (audio callback) | ✅ `audioCallback`, `Recorder::process`, `ClockSmoother`, `FrameClock::publish`: no locks, allocation or I/O; seqlock and RtMidi-callback ring ordering correct |
| REC-01a / 02a (tracks, Record) | ⚠️ works (Xvfb test), but GUI state bugs → **J2**, **J3** |
| REC-02 / 04 (sync, MIDI timeline) | ⚠️ end of the MIDI takes taken from the first audio take → **J4**, **J5** |
| REC-04a (clock) | ✅ as specified; side effect: after each dropout 0.5 s of raw (jittery) timing |
| REC-04b / 04c (SMF, rewrite) | ✅; robustness → **J9**, **J10** |
| REC-07 (folder) | ⚠️ non-ASCII names on Windows → **J6**; hand-written Windows paths → **J7** |
| Spec ↔ tests | ✅ every Spec item has a test or is a design rule / sub-item (TECH-*, the "a" items); NF-01 via T-REC-05/08 |
| Plan | ⚠️ PX row does not list #4 yet; P6c partly done (Linux) |
| Docs | ❌ **J1**: `Tests.md` had lost most of its content |
| Repository | ⚠️ commits `d2ca5b2` and `c80053f` are not on GitHub (last push did not arrive) |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| J1 | high (docs) | Commit `341fc26` cut `Tests.md` from 163 to 59 lines: the script that added T-REC-16..22 dropped everything after the new rows (Monitoring, Playback, VST3, Non-functional, Project, Text size, Licensing, CI tables and the whole Results history). | **Done:** rebuilt from `b571a1e` + the P6 rows + the 11 results since; diff against `b571a1e` shows only the intended changes. All tracked files checked for other large shrinks: none (README: GitHub's initial commit; `devices.cpp`: code moved to `console_capture.hpp`). |
| J2 | medium | `syncMidiPorts` clears `midiPortsDirty` even when a port failed to open (unplugged, busy). Plug it in and press Record: "MIDI input … is not open", with no retry until a track is re-armed. | Keep the flag set while a wanted port is not open; always sync ports in `toggleRecording`. |
| J3 | medium | `ensureDefaultTracks` replaces the track list the first time the stream runs. If the stream failed at start, tracks the user added meanwhile are wiped (and a MIDI port stays open). | Create defaults only if no track exists yet; mark ports dirty. |
| J4 | medium | Session end frame = length of the **first** audio take. If that take stops on a disk error, all MIDI takes are cut to its length. | Use the frames captured by the recorder (the timeline length), not one take's file length. |
| J5 | low | `stop()` reads the timeline start before stopping the recorder; Stop within the first audio block gives start 0 and drops all MIDI. | Read the start after `stopRecording()`. |
| J6 | low (Windows) | Track names and the folder are UTF-8 (ImGui) but become paths through the ANSI code page on Windows: "Gitaar é" gives a wrong or failing file name. Device names have the same issue. | Convert UTF-8 → path explicitly (`u8path` / `char8_t`) in one helper. |
| J7 | low | Quoted config values: `\\` still becomes `\`, so a hand-written UNC folder `"\\server\share"` loses a backslash, and `"D:\Takes\"` ends in `\"` (read as an escaped quote → warning). | Accept a final `\"` as the closing quote, and document "use `\\` for a double backslash" (or a raw-path rule) in the config comments and README. |
| J8 | low | A closed stream during a recording (not reachable from the GUI today) would end the session with an empty audio result and overwrite the MIDI files with empty ones. | Skip the final MIDI write when the stream is gone; keep the last periodic file. |
| J9 | low | MIDI files are rebuilt from the full event list each second (copy, sort, write, sync) on the thread that also empties the rings; long dense takes grow the cost; a long sync stall could overflow a ring (counted). | Keep the list sorted as it grows; rewrite less often for big takes. |
| J10 | low | First MIDI write error is kept for the whole take even if later writes succeed (e.g. Windows rename blocked while another program has the `.mid` open). | Clear the error once a later write succeeds; show "retrying" meanwhile. |
| J11 | low | A MIDI message that cannot be placed (no clock yet) is skipped without being counted; lost MIDI messages are shown only after Stop, not live. | Count it as lost; show lost MIDI messages next to lost frames while recording. |
| J12 | low | Until the stream reports its actual rate, the smoother uses the requested rate (only the 0.5 s warm-up affected); `--record-test` silently clamps a bad number of seconds. | Reset the smoother when the rate is known; reject bad values. |
| J13 | test gap | No unit tests for `RecordingSession` (start/stop, MIDI-only, start failures, end-frame rule) or `MidiInputs`; `buildMidiTake` lacks cases for orphan note-offs, a repeated note-on, an empty range. | Add a small test seam (the session talks to an interface instead of the concrete `AudioStream`/`MidiInputs`) and the missing cases. |
| J14 | spec | Not stated: MIDI before the first recorded audio block (up to one buffer after Record) is dropped, so a take can start with a note-off. | Add to SPEC-REC-04a. |

## Actions taken (user: "yes fix J2–J14, medium ones first")
| # | Done |
|---|------|
| J2 | `syncMidiPorts` retries a port that did not open every 2 s, and always right before Record. Manual test T-REC-29. |
| J3 | The default track is added only when the stream first runs and no track exists yet. T-REC-29. |
| J4 | End of the MIDI takes = timeline start + all frames the recorder took in (not the first take's file length). T-REC-26. |
| J5 | Start and end are read after the recorder has stopped. T-REC-26 ("Stop before any audio"). |
| J6 | `path_util.hpp`: `pathFromUtf8` / `pathToUtf8`, used for the folder, take files and every path shown or put in a message. T-REC-28 (also under Wine). Device names were already UTF-8 (RtAudio converts them) and never become paths, so they are not affected. |
| J7 | A final `\"` closes a quoted value (only when no other quote follows on the line, so device names like `Mic \"Pro\"; USB` still work); comments in the default config and the Spec explain paths. T-REC-22 extended, incl. save/load round trips. |
| J8 | `AudioStream::close()` keeps the result of a recording it ended; `stopRecording()` returns it. A MIDI-only recording whose stream is gone keeps the last periodic MIDI file. T-REC-26. |
| J9 | The session keeps each port's events sorted as they arrive; `buildMidiTake` no longer copies and sorts sorted input. Rewrite interval unchanged (≈ 1 s, SPEC-REC-04c). |
| J10 | Periodic MIDI write errors are shown as "… (retrying)" and cleared when a later write succeeds; only a failed final write is an error. T-REC-26. |
| J11 | Unplaceable MIDI messages are counted as lost; lost MIDI is shown live in the Recording line. T-REC-26. |
| J12 | The clock smoother restarts when the device's actual rate becomes known; `--record-test` / `--audio-test` reject bad seconds (exit code 2). |
| J13 | `session_io.hpp`: `SessionAudio` / `SessionMidi` interfaces (implemented by `AudioStream` / `MidiInputs`); `test_session.cpp` with stand-ins: T-REC-23..26, T-REC-28; T-REC-27 for the builder. `MidiInputs` itself is still only tested live (Midi Through). |
| J14 | SPEC-REC-04a states what happens before time 0 and how the end is chosen. |

Result: Linux 88/88 ctest; MinGW build under Wine 87/87 Catch2 cases; live MIDI regression on the VM: gaps within 0.52 ms with a 1024-frame buffer.
