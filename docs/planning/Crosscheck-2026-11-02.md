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

---

# Crosscheck #5 — 2026-11-02 (after P7 and P7b)

Scope: Spec v0.16, Plan, Tests, README, code at commit `bc3d8e5` (changes since `d6874b7`: monitoring, meters, MIDI hot-plug, log). Spec ↔ Plan ↔ Tests ↔ code checked by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Findings K1, K2 and K4 were checked again in the code.

| Area | Verdict |
|------|---------|
| NF-01 (audio callback) | ✅ monitoring mix, gain ramp, output peaks: no locks, allocation, I/O or log calls; gains via atomics; channel counts clamped (32 in, 2 out) |
| MON-01a / 02a | ✅ as specified (live on the VM); ⚠️ NaN input reaches the output → **K4** |
| REC-01b (hot-plug) | ✅ no data race: the port list only changes outside recordings, the session thread only reads rings and atomics, the old RtMidi thread is joined before a new one writes the ring; ❌ identical devices → **K1**; ⚠️ base-name rule too loose for backends without RtMidi numbering → **K2** |
| LOG-01 | ⚠️ `--audio-test` / `--record-test` do not log the device scan and the audio open, a disk error during a recording reaches the log only at Stop, "Rescan devices" is not logged → **K3**; a failed rename of the old log goes unnoticed → **K5** |
| GUI | ✅ Begin/End and Disabled pairs balanced, IDs distinct, status as text (UI-07); ⚠️ "Open log folder" waits for `xdg-open` on the GUI thread → **K6** |
| Spec ↔ tests | ✅ every Spec item has a test or is a design rule / sub-item (TECH-*, the "a" items); T-UI-09 can now be run → **K8** |
| Plan | ⚠️ PX row does not list #5 yet |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| K1 | medium | Two identical MIDI devices on Windows ("X 0", "X 1"), both armed. Unplug the first: WinMM renumbers the survivor to "X 0". Track A's handle (opened as "X 0") matches the survivor's new name and stays "connected"; track B's also matches by base name. The unplug is not noticed and not counted, so the take loses A's MIDI silently. With three identical devices the opposite happens: the base name is ambiguous, so a working port is closed as "disconnected" and never reopened. | Check connected ports per base name by **count**: still connected while at least as many ports with that base are present. When the count drops and more than one open port shares the base, mini-daw cannot tell which one went: close all of them with a warning and do not reopen them automatically (the user picks the ports again, so a track never silently records the other keyboard). Write this limit into SPEC-REC-01b; tests for 2 and 3 identical devices. |
| K2 | low (now) | `midiPortBaseName` strips any trailing number. On a backend that adds no number (CoreMIDI, JACK: P12) "Synth 1" and "Synth 2" both become "Synth", so a track could open the other synth. On WinMM and ALSA the RtMidi suffix is always there, so not reachable today. | Strip only the suffix the backend adds: WinMM " N", ALSA " C:P", none elsewhere (explicit naming style, tested for all three). |
| K3 | low | Log gaps against SPEC-LOG-01: (a) `--audio-test` / `--record-test` do not log the device scan or the audio open result; (b) a disk error during a recording is logged only at Stop (lost if the program crashes first); (c) "Rescan devices" is not logged. | Log them: scan + open result in both test modes, the disk error when it first appears, the rescan. |
| K4 | low | `mixMonitor`: `std::clamp(NaN)` returns NaN, so a NaN input sample (float driver glitch) goes to the output. | Replace NaN by 0 before limiting; test. |
| K5 | low | `Log::open` ignores a failed rename (e.g. a second mini-daw has the log open on Windows); the previous run's log is then lost or the open fails without saying why. | Report the rename failure as a warning in the new log (or the open error). |
| K6 | low | "Open log folder" (Linux) waits for `xdg-open`; on a desktop where it stays in the foreground the GUI freezes until the file manager closes. | Do not wait: reap the child later without blocking. |
| K7 | low (design) | The output meter is fed from the limited output, so it never shows how far the monitor mix went over 0 dBFS (CLIP still shows). The overload value `mixMonitor` returns is only used as "something was mixed". | Feed the meter with the value before limiting (per channel), so the dBFS text shows the overload. |
| K8 | docs | T-UI-09 (meters readable without colour) was never recorded; the screenshots of 2026-11-02 show dBFS text, CLIP, "armed", "on"/"off" and "DISCONNECTED" as text. | Record T-UI-09 as passed on Linux; Windows with the user's next test. PX row: add #5. |
| K9 | test gap | `MidiInputs` itself (setPorts, checkConnections, reconnect) has no automated test; T-REC-30 uses the stand-in. Tested live with `tools/midi_test/vkeys.c` only. | Keep the live test (documented in `CLAUDE.md`); an automated test with an RtMidi virtual port on Linux is possible but needs sequencer access in CI, so not now. |
| K10 | note | `openPort` runs on the GUI thread; a driver that hangs on open would stall the window every 2 s. Not seen. | No change now; a worker thread if it shows up. |

## Actions taken (user: "yes fix K1–K8, K1 first")
| # | Done |
|---|------|
| K1 | `planMidiCheck` (pure, unit-tested): connected ports are checked per base name by count; renumbered ports keep their open handle under the new name; when the count drops and several open ports share the name, all are closed and blocked ("CLOSED - arm off and on", logged), never reopened as the other device; each available port serves one track port. T-REC-33 (2 and 3 identical devices). |
| K2 | `MidiNaming` (WinMM " N", ALSA " C:P", Plain = none), native per platform; the base-name fallback only applies when the backend's number is there. T-REC-31 extended ("Synth 1" never opens "Synth 2"). |
| K3 | `--audio-test` / `--record-test` log the device scan and the audio open; a disk error during a recording is logged when it appears (GUI and `--record-test`); both "Rescan devices" buttons log the new list. Checked live in `--record-test`. |
| K4 | `mixMonitor` turns a NaN output sample into silence before limiting. T-MON-04. |
| K5 | `Log::open`: if the previous log cannot be renamed, the new run is appended after a marked warning line (new `openForAppending`). T-LOG-05 (also under Wine). |
| K6 | "Open log folder" (Linux/macOS) no longer waits for `xdg-open`; earlier children are reaped without blocking. |
| K7 | `mixMonitor` reports the per-channel peak before limiting; the callback feeds the output meter with it (no second pass over the buffer), so the dBFS text shows an overload. T-MON-04. |
| K8 | T-UI-09 recorded as passed on Linux; PX row lists #5. |
| K9, K10 | No change (see above). |

Result: Linux 99/99 ctest; MinGW build under Wine 98/98 Catch2 cases (1088 assertions); live hot-plug regression on the VM.

---

# Crosscheck #6 — 2026-11-02 (after P8)

Scope: Spec v0.18, Plan, Tests, code at commit `2892d9e` (P8: changes since `42e3402`), CI run 41532845975. Spec ↔ Plan ↔ Tests ↔ code by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). M1, M2, M5 and the CI failure were checked again in the code.

| Area | Verdict |
|------|---------|
| CI | ❌ run 41532845975: Windows 107/108 — T-PLAY-11 fails → **M0** (test bug); Linux 108/108; MSVC no warnings |
| NF-01 (audio callback) | ✅ `Player::process`, `limitOutput`: no locks, allocation or I/O; blocks above 8192 frames split |
| Player stop / MIDI thread | ✅ hazard pointer correct (store, re-check, seq_cst); every path that frees the run stops the MIDI sender first; `AppState` destroys `midiPlayer` first |
| PLAY-02a (streaming, sync) | ✅ deficit/skip keeps sync, negative starts, >2 channels; ⚠️ no-output path does not pay back the deficit → **M7** |
| PLAY-01a (transport) | ❌ Play at the end does nothing → **M2**; ⚠️ MIDI tail cut by the end check → **M3**; Remove during playback → **M4** |
| PLAY-03a / 04a (MIDI, mix) | ⚠️ mute drops controllers too → **M5**; two tracks on one port/channel share held notes → **M8** |
| PLAY-05 (overdub) | ✅ signs and kinds of shift right; ⚠️ overdub decided on offered, not started clips; fallback start 0 → **M6** |
| PLAY-06 (take folder) | ❌ can be opened during a recording → **M1** |
| Spec ↔ tests | ⚠️ T-DEV-07 used twice → **M10**; ✅ otherwise every Spec item has a test or is a design rule / sub-item |
| Docs ↔ code | ✅ latency formulas, output chain, Space guard as specified; ⚠️ `latency_correction_ms` only in the config file → **M11** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| M0 | high (CI) | T-PLAY-11 fails on real Windows: "Gitaar Ã©" instead of "Gitaar é". The test built the file name from a narrow `std::string`, which Windows converts with the ANSI code page, so the file on disk had the wrong name. mini-daw's own reading (`pathToUtf8`) is right; Wine did not show it (UTF-8 code page). | Create the test files through `pathFromUtf8` (as T-REC-28 does). |
| M1 | medium | The take browser window stays usable during a recording (only the menu item is disabled): Open or double-click calls `stopPlayback()` (ends the timeline run mid-take) and replaces the track list, so the new takes are placed at the wrong time on the wrong tracks. | Refuse in `openTakeFolder` while recording; disable the list and Open meanwhile. |
| M2 | medium | Play when the position is at or past the end (after playing to the end, or after a recording) starts and stops in the same frame: nothing is heard until Home. | Play from 0 when the position is at or past the end. |
| M3 | low-medium | The end check compares the audio position with the end of the takes; MIDI is sent `midiDelay` (tens of ms) later, so the last events of a MIDI take are not sent (Stop's note-offs cover the notes). | Stop at the end + the MIDI delay + 0.1 s. |
| M4 | low-medium | Remove is only disabled while recording; a track removed while playing keeps playing (and cannot be muted) until the next start. | Disable Remove while playing too. |
| M5 | low | A muted MIDI track drops all messages, also program changes, controllers, pitch bend and sustain-off; after unmuting the patch can be wrong or the pedal stuck. | When muted, drop only note-ons. |
| M6 | low | `overdub` is set from the clips offered, not from those that actually started (a clip skipped for its sample rate still causes a shift). If no timeline anchor exists at Stop (Stop before the first block), the take is placed at 0 instead of the play position where recording started. | `Player::start` returns how many clips started; remember the start position as the fallback. |
| M7 | low | `Player::process` with no output channels reads the rings without paying back an earlier deficit. Not audible (no output), but the rings get out of step. | Pay back the deficit first, as with output. |
| M8 | low | Held notes are keyed by (port, channel, note): two tracks playing to the same port and channel release or mute each other's notes. | Key held notes by track too. |
| M9 | low | Robustness: a WAV header claiming thousands of channels makes the reader allocate hundreds of MB (cap at 64); take lengths are not recomputed after a sample rate change (MIDI positions stale); `takeStart` < 0 parts can never be heard (position starts at 0) — acceptable, noted. | Cap channels; recompute take lengths after the audio settings change. |
| M10 | docs | T-DEV-07 is used twice (the `--audio-test` on Windows test and the new watchdog test). | Rename the watchdog test to T-DEV-08 (definition, result row, conversation log). |
| M11 | low (UI) | `latency_correction_ms` (overdub calibration) can only be changed in `config.ini`. | Add it to Settings → Recording…. |
| M12 | test gap | No test stops/starts the player while a fake audio thread runs (the hazard pointer is only exercised sequentially); none for one output channel, >2-channel files or blocks above 8192 frames; overdub placement lives in `main.cpp` and has no unit test. | Add the player tests; move the placement rule into a core function with a test. |

## Actions taken (user: "yes fix M0–M12, M0 first")
| # | Done |
|---|------|
| M0 | T-PLAY-11 creates the non-ASCII file through `pathFromUtf8` (the only such test; T-REC-28 already did). |
| M1 | `openTakeFolder` refuses while recording; the browser's list and Open are disabled with a note. |
| M2 | Play at or after the end starts from 0. |
| M3 | Playback stops at the end + the MIDI delay + 0.1 s. |
| M4 | Remove (X) is disabled while playing. |
| M5 | A muted MIDI track drops only note-ons; T-PLAY-17. |
| M6 | `Player::start` returns the clips started; overdub only when audio clips or MIDI tracks really play; placement in the new core function `placeRecordedTake` with the Record position as fallback; T-PLAY-16. |
| M7 | No-output path pays back the deficit first; T-PLAY-15. |
| M8 | Held notes keyed by track as well; T-PLAY-17. |
| M9 | WAV reader refuses more than 64 channels (T-PLAY-05); take lengths recomputed after the audio settings change. `takeStart` < 0 parts stay unheard (noted, by design). |
| M10 | Watchdog test renamed T-DEV-08 (definition, result, log). |
| M11 | Settings → Recording… has "Overdub latency correction (ms)". |
| M12 | T-PLAY-14 (stop/start under a running audio thread, also with ThreadSanitizer), T-PLAY-15 (one output, 3 channels, big blocks, no output), T-PLAY-16 (placement), T-PLAY-17 (MIDI). |

Result: Linux 113/113 ctest; MinGW build under Wine 112/112 Catch2 cases; ThreadSanitizer clean on the threaded tests.
