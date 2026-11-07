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

---

# Crosscheck #7 — 2026-11-03 (after P9)

Scope: Spec v0.20, Plan, Tests, README, code at commit `6536815` (P9: changes since `5122c25`), CI run 41540018781. Spec ↔ Plan ↔ Tests ↔ code by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). N1 and N2 were reproduced with a small program; N3 checked in the code.

| Area | Verdict |
|------|---------|
| CI | ✅ run 41540018781: every step of the linux, windows and licence jobs passed (nlohmann/json fetched and checked); the job logs could not be downloaded here (network error), so the test count and MSVC warnings are not confirmed |
| PRJ-01a/02a (saving) | ✅ no autosave over a good file: nothing is saved without an open project, the old project is saved before switching, tmp + sync + rename; ❌ invalid UTF-8 makes saving throw → **N2** |
| PRJ-02a (opening) | ❌ a wrong-typed `format`/`version` throws (crash at every start) → **N1**; ⚠️ repairs not written, skipped tracks lost on the next save → **N6**; failures not shown (Recent, start) → **N5** |
| PRJ-01a (copying takes in) | ❌ name collisions in `takes/imported/` → **N3**; ⚠️ a failed copy still switches the project → **N4** |
| Paths | ✅ relative/absolute, other drives, `/` separators; ⚠️ trailing separator, relative typed folders, take == folder → **N8** |
| Spec ↔ tests | ✅ every Spec item has a test or is a design rule / sub-item; no duplicate IDs; no result rows for unknown tests |
| Docs | ⚠️ stale "track list in memory only" text; REC-07 silent about project takes; default config without `[project]` → **N10** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| N1 | high | `projectFromJson` uses `json::value()` for `format` and `version`, which throws when the key has another type (`"version": "1"` → `type_error.302`, reproduced). The promise "never throws" is broken; since the last project is opened at start, such a file crashes mini-daw at every start. | Check the types explicitly; catch any exception around the whole parse (→ error "damaged project file"); also guard the open at start. Test with wrong-typed `format`/`version`/`tracks`. |
| N2 | high | `projectToJson` → `dump()` throws on a string that is not valid UTF-8 (`type_error.316`, reproduced with "Pr\xfcfung"). Autosave runs every 0.5 s without a catch: such a track name or MIDI port name (from an old config, a driver, a hand-edited file) crashes the program, also at exit. | `dump(…, error_handler_t::replace)` (bad bytes become U+FFFD); catch exceptions in `saveProjectNow` and report them. Test. |
| N3 | medium | Copies go to `takes/imported/<parent folder>/<file>`, and an existing file there of the same size is taken as the same take. Two different takes with the same parent folder and file name (e.g. `take-…/01-Gitaar.wav` from two projects): equal size → the track silently plays the other recording; different size → the earlier copy is overwritten while another track may still use it. On a failed copy, `remove(dest)` can delete a good earlier copy. | Never overwrite: reuse only a file with the same size **and** content; otherwise pick a free name (`-2`, `-3` …); remove only a file this call created. Test with a collision. |
| N4 | medium | Save as / New with copying: when a take cannot be copied (disk full, missing), the project is switched anyway and keeps an absolute path to the outside take. | Keep the dialog open with the errors and do not switch (the new folder stays, without project.json, for another try); a missing take (nothing to copy) is only reported. |
| N5 | medium | Open via File → Recent or at start: an error goes to the dialog's error text, which is not shown when the dialog is closed (only the log has it); a dead entry stays in the Recent list. | Show failures as project notes in the status line; mark or drop dead Recent entries. |
| N6 | medium | After opening, the "saved" text is the repaired state, so the repairs are not written; on the next change autosave writes it and tracks that were skipped (unknown kind) are gone from the file without notice. | When opening needed repairs or skipped something: keep the original as `project.json.bak` and say so; then save the repaired state. |
| N7 | low | Wrong numeric types accepted quietly: a negative input channel wraps to a huge number (rejected only when recording), a fractional MIDI channel is truncated. | Check the channel list (unsigned, < 64) and ranges; warn. |
| N8 | low | Paths: a folder typed with a trailing separator gives an empty name (title, "… copy"); a relative typed folder is stored as such in `last`/`recent` (depends on the working folder); a take path equal to the project folder counts as inside. | Make typed folders absolute and normal (no trailing separator); a take must be a file below the folder. |
| N9 | low | Save as / New into a folder inside the current project is allowed (nested project). | Refuse a target inside the open project. |
| N10 | docs | SPEC-REC-01a and SPEC-PLAY-01a still say the track list is kept in memory only until P9; README says "The track list is not saved yet"; SPEC-REC-07 does not say that takes go into the open project; the default `config.ini` text has no commented `[project]` section; CI run 41540018781 not in Tests.md. | Update the texts, add `[project]` to the default config, log the run (with the log-download note). |
| N11 | test gap | No tests for N1, N2, N3, a failed copy (N4), or the repaired-state save (N6). | Add them with the fixes. |
| N12 | note | Copying takes in runs on the GUI thread (the window waits on a big take; written in the Spec); a failing save is retried every 0.5 s (log deduplicated); the folder is not synced after the rename; `takes/imported/` is not in the take browser (only `take-*`). | No change now. |

## Actions taken (user: "yes fix N1–N11, N1 and N2 first")
| # | Done |
|---|------|
| N1 | Header fields checked by type before use; the whole parse runs inside a catch-all → "The project file is damaged (…)"; a damaged last project at start leaves mini-daw running with a note. T-PRJ-03 (version "1" / 1.5, format 5, tracks an object, an array, empty text). Checked live: start with such a file → running, note in the status line. |
| N2 | `dump()` with `error_handler_t::replace` (bad bytes → U+FFFD). T-PRJ-08. |
| N3 | `importTakes` never overwrites: reuses a file only with the same content (`sameFileContent`), else a free `-2`, `-3` … name; removes only a file it started. T-PRJ-07. |
| N4 | `importTakes` returns copied / missing / failed; Save as only switches when nothing failed (the dialog stays open with the reason). T-PRJ-07 (failed copy). |
| N5 | Open failures also go to the status line (Recent, start); Recent marks missing projects "(not found)" and disables them. Checked live. |
| N6 | When opening needed repairs: original kept as `project.json.bak`, the repaired state saved by the next autosave. Checked live (unknown kind "video": skipped, .bak keeps it). |
| N7 | Input channels: 1–2 whole numbers 0..63, else warning and input 1; MIDI channel 0..16 integer, else warning and all. T-PRJ-03. |
| N8 | Typed folders absolute + normal, trailing separator removed (`cleanFolder`); the folder itself is not "inside". T-PRJ-04. |
| N9 | New / Save as refuse a folder inside the open project (or the project itself). |
| N10 | SPEC-REC-01a, PLAY-01a, REC-07 and README updated; default config has a commented `[project]` section; CI run 41540018781 logged with the log-download note. |
| N11 | T-PRJ-03 extended, T-PRJ-04 updated, T-PRJ-07, T-PRJ-08 new. |
| N12 | No change (noted). |

Result: Linux 119/119 ctest; MinGW build under Wine 118/118 Catch2 cases; live checks of N1, N5, N6 on the VM.

---

# Crosscheck #8 — 2026-11-03 (after P9b)

Scope: Spec v0.22, Plan, Tests, README, code at commit `497c975` (P9b: changes since `f863bda`), CI run 41606309851. Spec ↔ Plan ↔ Tests ↔ code by Claude; the packaging changes also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). The CI failure was read from the job log (fetched over HTTP/1.1; `gh` fails on this network); O7 checked in GLFW's source.

| Area | Verdict |
|------|---------|
| CI | ❌ run 41606309851: windows — Install NSIS (Chocolatey, NSIS 3.13), Configure, Build (MSVC: mini-daw.exe + mini-daw.com), Test, PE check and `.com` smoke run pass; **Build the installer fails** → **O0**; upload skipped. linux and licence pass |
| PKG-01 (two programs) | ✅ OBJECT library compiled once; WIN32 + `/ENTRY:mainCRTStartup`; PE check passes on MSVC too; ⚠️ console attach looks at stdout only → **O6**; ⚠️ `.ilk` name clash in MSVC Debug → **O3** |
| PKG-02 (installer) | ❌ never built by the CMake target on Windows → **O0**; ⚠️ running instance, older over newer → **O4**, **O5**; ⚠️ CI artifact layout → **O1** |
| PKG-03 (icon) | ✅ all 7 sizes in the .ico, regeneration byte-identical; GLFW_ICON is the name GLFW loads |
| CI-01 | ⚠️ NSIS from Chocolatey unpinned → **O2** |
| Spec ↔ tests | ✅ every Spec item has a test or is a design rule / sub-item; no duplicate IDs; no result rows for unknown tests; docs use `mini-daw.com` for the options |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| O0 | high (CI) | `makensis` on Windows: `File: "D:/a/…/installer-stage/mini-daw.exe" -> no files found`. The Windows makensis splits a `File` path at the last backslash; the path had only `/`, so it looked for the whole string as a name in the current folder. On Linux (`/` is the separator) the same script works, which is why the container test passed; the CMake `installer` target itself had not been run on Windows before. | In the script, join the file name with `\` when `NSIS_WIN32_MAKENSIS` is defined (else `/`); pass the stage folder in native form (`file(TO_NATIVE_PATH)`). Check: container makensis (Linux) locally, CI (Windows). |
| O1 | medium | With three paths, `upload-artifact` keeps them below their common folder: the download holds `src/Release/mini-daw.exe`, `src/Release/mini-daw.com` and `mini-daw-0.1.0-setup.exe` (before: a flat `mini-daw.exe`). | Copy the three files into one folder in the workflow and upload that folder (flat download). |
| O2 | low-medium | `choco install nsis` takes the newest package (3.13 in this run): not reproducible, unlike the SHA-pinned actions. | Pin `--version=3.13.0`; check the version makensis reports; note it in SPEC-CI-01. |
| O3 | low | MSVC Debug builds: `PDB_NAME` separates the PDBs, but both programs would write `mini-daw.ilk` in the same folder (incremental linking). Release (CI, installer) is not affected. | `/INCREMENTAL:NO` for the `.com`. |
| O4 | low-medium | Installing while mini-daw runs: `File` cannot replace the open program; the user gets Abort/Retry/Ignore, a silent install fails. | Before copying, ask to close mini-daw (retry until it is closed, or cancel); detect it by trying to rename the installed program. |
| O5 | low | An older installer can overwrite a newer version without notice; reinstalling into another folder leaves the old files. | Compare with the installed version (registry `DisplayVersion`) and ask before installing an older one; on a new folder, offer to remove the old installation (run its uninstaller). |
| O6 | low | `attachToParentConsole` only checks stdout: with `mini-daw.exe … > file` stdout is redirected and nothing is attached, so error messages on stderr are lost; when mini-daw.exe is started from a terminal with only `--config`, the GUI attaches to the terminal (Ctrl+C there reaches the GUI, GLFW messages appear). | Attach only for the options that print (not for `--config` alone), and reopen only the stream that has no handle. |
| O7 | low | GLFW makes the process per-monitor DPI aware (`win32_init.c:692`), so Windows does not stretch the window (no blur), but mini-daw draws its text at 16 px in physical pixels: on a 150 % display it is small until the user raises it (View → Text size). There is no manifest (GLFW sets DPI awareness at run time, so none is needed for that). | When there is no `font_size` in the config yet, start with 16 px × the window's content scale (`glfwGetWindowContentScale`), rounded, within 12–32. |
| O8 | docs | SPEC-PKG-02 says "Installing over an older version replaces it" without the running-program case; Tests have no row for the CI installer build. | Update with O4/O5; T-PKG-03 also covers the CI-built installer. |

## Actions taken (user: "yes fix O0–O8, O0 first")
| # | Done |
|---|------|
| O0 | The NSIS script joins file names with `\` when `NSIS_WIN32_MAKENSIS` is defined (else `/`); CMake passes native paths. Linux makensis builds it; the Windows makensis proof comes from CI (running a downloaded Windows makensis here was refused). |
| O1 | CI copies mini-daw.exe, mini-daw.com and the setup into `upload/`; the download is flat. |
| O2 | `choco install nsis --version=3.13.0`; the step fails unless makensis reports v3.13; SPEC-CI-01 note updated. |
| O3 | `/INCREMENTAL:NO` for mini-daw.com (no shared `.ilk` in Debug). |
| O4 | Install and uninstall check whether mini-daw.exe / .com can be opened for writing; if not: "mini-daw is running. Close it…" (Retry/Cancel), silent: abort, exit 2. Checked under Wine with mini-daw.exe running. |
| O5 | `.onInit`: an older version over a newer one asks (silent: refuses); an earlier installation in another folder is uninstalled first. Checked under Wine. |
| O6 | Attach only for options that print (not `--config` alone); reopen only the stream without a handle. |
| O7 | Text, spacing (ScaleAllSizes + FontScaleDpi) and the first window size follow the content scale (window at most 90 % of the work area); follows a monitor change; `font_size` stays the 100 % size. Checked on Linux at 150 %. |
| O8 | SPEC-PKG-01/02, UI-08, CI-01 and T-PKG-03, T-UI-13 updated. |

Result: Linux 119/119; installer rules checked under Wine; 150 % scaling checked on Linux; the Windows makensis build to be confirmed by CI.

# Crosscheck #9 — 2026-11-04 (after P10: VST3 instruments and editor windows)

Scope: Spec v0.26, Plan, Tests, README, THIRD_PARTY.md, CLAUDE.md, code at commit `abdf558` (changes since `a5c68a8`: P10a–e, the error-dialog fix, the meter width fix), CI runs 41703163695, 41734020005, 41736545593 (all green). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the P10 code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Every reviewer finding below was checked against the code by Claude; one proposed fix (Q5) is disputed.

| Area | Verdict |
|------|---------|
| CI | ✅ three green runs; Windows 130/130 including the editor in a real window (T-VST-19) |
| VST-01 (scan) | ✅ moduleinfo.json, cache, error dialogs suppressed; ❌ a module that crashes mini-daw in its **first** scan is not recorded → scanned (and crashes) again → **Q2** |
| VST-02 (instrument, rack) | ✅ hazard-pointer publish/retire, unload only after detach, VST3 setup/state/unload order; ❌ allocation in the audio callback → **Q1**; ⚠️ hung live notes and per-slot state after reconfiguration → **Q3**; ⚠️ parameter points dropped → **Q4**; ⚠️ take timing after a rate change → **Q6** |
| VST-05 (editor) | ✅ view removed while its window exists, frame cleared, closed before the instrument unloads; ⚠️ `restartComponent` ignored → **Q5** |
| VST-06 (state) | ✅ component, then controller (`setComponentState`, `setState`); ⚠️ a wrong-typed instrument field fails the whole project → **Q8** |
| Audio-thread rules | ❌ Q1; otherwise no locks, no I/O; parameter queues and event list capped as designed |
| Spec ↔ tests | ✅ no duplicate IDs, no result rows for unknown tests; ⚠️ SPEC-LIC-05 has no test; MIDI mapping, controller state, crash marker in the app untested → **Q11**, **Q12** |
| Docs | ⚠️ README status and first "VST3" without ®; THIRD_PARTY.md without the trademark sentence; CLAUDE.md lacks the Wine display note → **Q11** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| Q1 | medium | `InstrumentRack::process` (`instrument_rack.cpp:122`) calls `std::stable_sort` on the block's events. libstdc++ and MSVC allocate a temporary buffer for it: allocation in the audio callback (design rule), a possible glitch with many notes. | Live events (offset 0) are already first and playback events already in time order: no sort needed, or an in-place insertion sort (stable, ≤ 512 elements). A test that counts allocations is not practical; keep the rule by construction and comment it. |
| Q2 | medium | `checkScanMarker` (`main.cpp:1084`) marks the crashed module only if it is already in the cache. The usual case is a crash during the module's first scan: the marker is removed, nothing is recorded, the next Scan loads it again and mini-daw crashes again (contradicts SPEC-VST-01a "marked crashed and skipped"). | Move this into the core (`PluginCache::recordCrash` or the scanner's result) so a missing module is added as Crashed; test in T-VST-11 (marker for a module not in the cache). |
| Q3 | medium | Notes can hang: (a) a live note held while the track is disarmed (or the slot reconfigured) loses its note-off — the ring is drained and live notes are not tracked; (b) `held_`, `wasPlaying_`, `curGain_` and `gains_` belong to the slot index and survive `configure()`: when tracks shift slots, playback note-offs go to the wrong instrument and the first block uses the previous track's gain; (c) more than 512 events in a block drops the rest, note-offs included. | Track live notes in `held_` too; give each slot an identity (track + instrument) and on change or live → off send note-offs to the old instrument first; reset the per-slot state when a slot changes owner; put the gain in `SlotSetup`; when the event list is full, keep note-offs (drop other events first). Tests in T-VST-14: held live note across disarm, slot shift. |
| Q4 | medium | `instrument.cpp:374`: a parameter queue takes at most 4 points per block, further changes are dropped — but they are still echoed to the controller. A fast editor drag or a stream of controller/pitch-bend messages leaves the processor at an older value than the editor shows. | When the queue is full, overwrite the value of its last point (the latest value wins); echo only what reached the processor. Test: 10 changes of one parameter in one block → the processor ends at the last value. |
| Q5 | low-medium (part disputed) | `restartComponent` flags are stored and never read. The reviewer suggests pushing all controller values to the processor on `kParamValuesChanged`; that is not what the flag means (it asks the host to re-read parameter values for its own displays — mini-daw has none) and could overwrite the processor's state. Real gaps: `kIoChanged` (busses changed) and `kLatencyChanged` (matters for P11 recording). | On `kIoChanged` / `kReloadComponent`: reload the instrument with its state (as after a rate change), from `idle()`; record `kLatencyChanged` for P11; `kParamValuesChanged`: nothing to do. Log every restart request. |
| Q6 | medium-low | `syncRack` caches a take's events per track keyed by the take path only (`main.cpp:1266`); the frames depend on the rate (`smfToFrames(d, instrumentRate)`). After a sample-rate change the old frames are used: an instrument plays a MIDI take about 9 % off in time (48 → 44.1 kHz). | Key the cache by path and rate (or clear it in `reloadInstruments`). |
| Q7 | low | `reloadInstruments` runs after every successful audio open, also when only the buffer size changed (instruments are set up for up to 4096 frames regardless): all plugins reload, editors close and reopen, a short silence. | Reload only when the rate changed. |
| Q8 | low | `project.cpp:252`: `ins.value("module", …)` throws for a wrong-typed field; the top-level catch then fails the whole project, unlike the per-field warnings (N1). | Type-check `module`, `name`, `state`, `controllerState` like the other fields; extend T-VST-15. |
| Q9 | low | `process()` echoes every parameter change to the controller, including those that came from the controller itself (editor edits): redundant, and a delayed echo can make a dragged control jitter. | Echo only changes that did not come from the controller (MIDI-mapped controllers, host changes). |
| Q10 | notes | `ProcessContext` claims a valid tempo of 120 BPM: consistent with mini-daw's MIDI timing (960 ppq at 120 BPM), no tempo setting exists yet. `configure()` waits for the audio thread; a plugin blocked in `process()` would freeze the GUI (accepted: in-process hosting, SPEC-VST-02a). `getState` every 5 s on the GUI thread may take a moment with large plugins (not observed). | None now; revisit with a tempo setting (later) and if users report stalls. |
| Q11 | docs | README: status "P2–P9b" and "its own editor window comes next"; its first line says "VST3" without ® (SPEC-LIC-05: first use carries ®). THIRD_PARTY.md (credits) lacks "VST is a registered trademark of Steinberg Media Technologies GmbH." SPEC-LIC-05 has no test. CLAUDE.md: the Wine test run needs a display (T-VST-19 fails without one). | Update README and THIRD_PARTY.md; T-LIC-06: `tools/check_licenses.py` checks the sentence in README, THIRD_PARTY.md and the instruments window source, and ® on the first "VST" in README; CLAUDE.md note. |
| Q12 | tests | Untested: the MIDI-mapping path (controllers, pitch bend, channel pressure, program change → parameters; the test synth has no `IMidiMapping`); controller state (the test synth's controller has no `getState`/`setState`); Q2–Q4, Q6. | Test synth: map CC 7 → Gain, add controller state (one value); tests for each fix above. |

## Actions taken (user: "yes fix Q1–Q12, Q1 first")
| # | Done |
|---|------|
| Q1 | No sort in the rack: the block's events are built in time order (note-offs and live input at offset 0, then the take's events in file order). |
| Q3 | Per-instrument audio state (`Voices`: held take notes, held live notes, playing / live flags, gain) found by instrument and load serial (`Vst3Instrument::serial()`), so it follows an instrument to another slot and is reset for a new one; live notes released when the track is disarmed; gains live in the published configuration (`SlotSetup::gain`, then `setGain`), so a new slot starts at its own gain; with a full event list a note-off takes the place of the last other event. T-VST-21. |
| Q2 | `recordScanCrash` in the core marks or adds the crashed module and removes the marker; the scanner and the app both use it. T-VST-11 extended (first-scan crash → skipped by the next scan). |
| Q4 | A full parameter queue gets the latest value at its last point's offset (replaced by the SDK, no allocation). T-VST-22 (fails without the fix). |
| Q9 | Edits from the controller are not echoed back; MIDI-mapped and host changes are (the editor shows them). |
| Q6 | The take event cache is cleared when the instruments' rate changes. |
| Q7 | `reloadInstruments` returns when every instrument already runs at the stream rate. |
| Q5 | `takeRestartRequest()` / `latencySamples()`; the GUI logs every request and reloads the instrument with its state (editor reopened) for kIoChanged / kReloadComponent; kParamValuesChanged needs nothing. Test synth hook; T-VST-23. |
| Q8 | `module`, `name`, `state`, `controllerState` type-checked: warnings for the track, the project opens. T-VST-15 extended. |
| Q10 | Notes only, no change. |
| Q11 | README: VST® on first use, status P2–P10, the Edit button; THIRD_PARTY.md: Trademarks section; T-LIC-06 in `tools/check_licenses.py` (checked to fail without the ®); CLAUDE.md: the Wine tests need a display. |
| Q12 | Test synth: controller 7 → Gain (IMidiMapping), controller state (editor size), restart hook. T-VST-21..23. Q6 and Q7 are in the GUI code (no unit test); checked by reading. |

Spec v0.27 (SPEC-VST-01a, 02a). Linux 131/131, Wine 131/131 (8 runs; one earlier failed assertion in a freshly made Wine prefix, not reproduced).

# Crosscheck #10 — 2026-11-05 (after P11: instrument sound recorded and rendered)

Scope: Spec v0.28, Plan, Tests, README, THIRD_PARTY.md, code at commit `0d1fbd9` (changes since `5cecb3d`), CI run 42023013738 (green). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the P11 code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Every reviewer finding was checked against the code by Claude; R3 changes the Spec (Claude's own placement rule was wrong).

| Area | Verdict |
|------|---------|
| CI | ✅ windows 139/139 (MSVC, no warnings), linux, licence |
| Audio thread | ✅ capture buffers made at start; the recorder reads the same block's capture; no lock, allocation or I/O; ⚠️ blocks over 8192 frames silently become silence → **R7** |
| VST-03a (live recording, linked track) | ❌ overdub placement of the instrument WAV → **R3**; ⚠️ silent take over a good one → **R4**; removing the linked track → **R2**; hand-edited armed linked track → **R9** |
| VST-04a (render) | ❌ a cancelled or failed re-render deletes the take that plays → **R1**; ⚠️ Record during a render → **R5**; leftovers → **R8** |
| Plan / docs | ⚠️ "P11b" used twice (the new live recording and the older ASIO step) → **R10**; SPEC-VST-04 still promises real-time rendering → **R11** |
| Spec ↔ tests | ✅ no duplicate IDs, no result rows for unknown tests; ⚠️ GUI rules untested → **R12** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| R1 | medium | The render file name is the same for every render of a track (`NN-<linked>-render.wav`), and after a first render it is the linked track's take. A second Render opens it for writing (truncating the file the player may be reading; on Windows the open can fail), and Cancel or a failure then deletes it: the linked track points at a missing file (`render.cpp:57, 113`). | Render into `…-render.tmp` and rename over the old file only on success (after stopping playback of that file); a cancelled or failed render removes only its `.tmp`. Test in T-VST-29: render, render again and cancel → the first file still there and intact. |
| R2 | medium | Removing the **linked** track while "Sound from: this recording" is set leaves the MIDI track with `soundFromRecording = true`: its instrument gets no take MIDI, and the switch was on the removed row, so nothing plays the take and nothing can change it (the project repair only helps after reopening). | On removing a linked track: set its MIDI track's `soundFromRecording = false`. A core helper (`removeTrack(tracks, index)`) doing both directions, tested. |
| R3 | medium | Overdub placement: the MIDI take is moved earlier by the MIDI latency estimate, the instrument WAV is not (`placeRecordedTake(…, overdub=false, …)`; SPEC-VST-03a says "no latency shift"). But the instrument played the same late notes: its sound is as late as the MIDI. With "Sound from: this recording" the overdub plays late by the latency compared to "Sound from: instrument". My rule in the Spec was wrong. | The linked WAV gets the same shift as its MIDI take (`takeStart` of the MIDI take). Spec: "placed like its MIDI take". Test: a core function for placement, or a session test with overdub. |
| R4 | medium | A take for the linked track is recorded whenever the MIDI track names an instrument (`recording_session.cpp:85`), even if the plugin did not load ("not loaded"): a silent WAV then replaces a good recorded or rendered take on the linked track. | Record the instrument's sound only if it is loaded: the GUI passes that (e.g. clear the field for tracks whose instrument is not loaded, or a `soundRecorded` flag in the track list given to the session), the same rule as for making the linked track. Test in T-VST-28. |
| R5 | low-medium | Record can start while a render runs (only Render is blocked during a recording). When the render ends it puts its file on the linked track, replacing the take just recorded (or being replaced by it). | Record waits: disabled while a render runs (with a tooltip), and Render disabled while recording (already). |
| R6 | low | With "Sound from: this recording" and the MIDI track armed, Record plays the old linked WAV while the live instrument sounds: doubled sound during the recording (overdub semantics, not specified). | During a recording, an armed instrument track's linked take is not played (as for audio takes of armed tracks); Spec. |
| R7 | low | A block longer than 8192 frames gives silence in the instrument WAV without a loss report (`instrument_rack.cpp: frames > kChunk`), against SPEC-REC-05 (loss always reported). | Count it as lost frames (a gap) instead of silence, so the take result reports it; such blocks are rare (devices allow up to 8192). |
| R8 | low | `startRender` makes the linked track before its checks: if the render cannot start (take unreadable, plugin refused), an empty linked track stays. A render whose track was removed and a new one made with the same number would attach to the new one (numbers are reused). | Check first, make the linked track only when the render starts; remember the render's target by track number and the take path, and attach only if both still match. |
| R9 | low | A hand-edited project with `"armed": true` on a linked track: the session would record an input take and the instrument take into the same file. | The project loader clears `armed` and `monitor` on linked tracks; the session ignores arming of linked tracks. |
| R10 | docs | "P11b" means both the new live recording step and the older "Optional ASIO backend" (Plan, THIRD_PARTY.md, the CMake message, the Spec's WASAPI note). | Rename the ASIO step to P14 everywhere (the new P11a–c stay). |
| R11 | docs | SPEC-VST-04: "offline or in real time" — only offline exists (the user's choice for P11, "Offline first"). | Spec: real-time rendering is not planned for now (offline covers it); note it in the Plan as an open idea. |
| R12 | tests | No tests for the GUI rules: removing either track of a link, Record vs. Render, the render target; none for more than 1024 events in a render block, the 10 s tail limit, or a re-render. | Move the rules into core functions where possible (R2, R3, R8) and test them; render tests for the event cap, tail limit and re-render (R1). |

## Actions taken (user: "yes fix R1–R12, R1 first")
| # | Done |
|---|------|
| R1 | Each render writes a new file (`newRenderPath`: `-render.wav`, `-render-2.wav`, …); a cancelled or failed render removes only its own new file. T-VST-29: render again and cancel → the first file intact. (Chosen over .tmp + rename: renaming over a file the player has open fails on Windows.) |
| R2 | `removeTrack` in the core: removing a linked track sets its MIDI track back to "Sound from: instrument"; removing a MIDI track unlinks its linked track. Used by the GUI; T-VST-24. |
| R3 | `placedAsAudioInput`: a linked track's take is placed like its MIDI take (overdub shift by the MIDI estimate); used for every new take. Spec changed. T-VST-24 (the rule). |
| R4 | `tracksToRecord`: instruments that are not loaded record no sound (the linked take stays). T-VST-24. |
| R5 | Record disabled (tooltip) and Ctrl+R refused while a render runs; Render also refused while recording. |
| R6 | While recording, an armed instrument track's old linked take does not play. |
| R7 | `InstrumentSound::Lost` for blocks over 8192 frames: the recorder counts them as lost frames (gap, silence in place). T-VST-25, T-VST-26. |
| R8 | Render checks the take and the instrument before making the linked track, removes a linked track it made when the plugin refuses, and keeps the result only if the MIDI track still has the same take. |
| R9 | The project loader clears `armed` / `monitor` on linked tracks; the session and `tracksToRecord` ignore their arming. T-VST-27, T-VST-28. |
| R10 | The ASIO step is now P14 (Plan, THIRD_PARTY.md, CMake message, Spec note); P11a–c keep their names. |
| R11 | SPEC-VST-04: done offline; real-time rendering not planned for now. |
| R12 | Tests as listed above; T-VST-29 also: 1500 events in one block (held back, note still sounds and ends), the tail limit. |

Spec v0.29. Linux 137/137, Wine 137/137 (2 runs).

# Crosscheck #11 — 2026-11-06 (after P16 auto record, window place, linked track at instrument choice)

Scope: Spec v0.32, Plan, Tests, README, CLAUDE.md, code at commit `030535a` (changes since `4be4d48`: `3cf8f21`, `4f1a628`, `d3ffcbf`, `030535a`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Every reviewer finding was checked against the code by Claude (S1–S4 confirmed at the lines named; S7 judged a known limit).

| Area | Verdict |
|------|---------|
| Audio thread | ✅ `Recorder::process`: only a peak scan and a relaxed atomic store added; no lock, allocation or I/O |
| REC-08 recorder parts mode | ✅ command order, history ring, cut-off at the end, stop with open/queued parts; ⚠️ loss in the preroll not counted → **S5** |
| REC-08 session / tracker | ❌ MIDI of an ended part can be pruned before it is written → **S1**; ❌ minimised window: no parts, start clamped silently, WAV and MIDI misaligned → **S2**; ⚠️ stream lost in a part → empty part **S3**; folder error retried every frame **S4**; boundary MIDI **S6**; channel filter **S8** |
| UI-09 / UI-10 / VST-03a / PLAY-05a | ✅ window place, hidden start, fixed area, linked track; ⚠️ overdub count includes MIDI tracks that play nothing → **S9** |
| Spec ↔ code ↔ docs | ⚠️ **S10** (texts), **S12** (README, CLAUDE.md) |
| Spec ↔ tests | ❌ T-REC-33 used twice (the new auto record tests and an existing port test; my first rename from T-REC-32 also collided) → **S11**; ⚠️ several parts-mode paths untested → **S11** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| S1 | high | The MIDI thread loads `keepFrom_` after it took the job list (`runMidiPartJobs`). If the GUI ends a part in between (a once-a-second refresh of the open part can take tens of ms), it sets `keepFrom_` to now − 5 s and the MIDI thread drops the events of the part it has not written yet: the part's `.mid` holds only its last seconds, without a report. | Read `keepFrom_` together with the job list under `partMutex_`, and keep from the earliest of it, the open part's start and every pending job's start. Test: a long part, the GUI ends it while a refresh runs. |
| S2 | high | While the window is minimised the main loop skips `updateAuto` (and on Windows a window move/resize blocks the loop): playing then starts no part until the window is back; the part then asks to start where the playing began, the recorder can only go 4 s back (silently), and the MIDI part starts at the unclamped frame, so the part's WAV and MIDI have different starts. | Run `updateAuto` also while minimised. The session clamps the part start to what the recorder still has (now − 3.5 s) before starting audio and MIDI parts, so both start at the same frame, and logs that the start was cut. Spec: the 4 s memory. |
| S3 | medium | If the audio stream is gone when auto record is stopped during a part, the part is ended at its own start: everything recorded in it is cut off without an error. | End it where the audio stopped (the recorder's last frame; for MIDI only the last message + 1 s) and report "the audio stream stopped during auto record". Test. |
| S4 | medium | When the part folder cannot be made (disk full, read-only), the tracker forgets the part but not the playing, so the next frame tries again: an error and a log line 60 times a second. | `AutoPartTracker::abort(lastActivity)`: the playing is accounted for; the next playing tries again. Test. |
| S5 | medium | Loss reporting in parts: audio lost in the 1 s before a part (in memory as silence) is not counted; lost frames that were cut off at the end are counted; after stopping, the lost frames and lost MIDI messages are not shown (only `res.error`), and MIDI loss is never shown in auto mode. | Mark lost frames in the memory and count them when a part takes them; count lost frames only up to the part's end; show lost frames and lost MIDI messages while auto record runs and in the summary at stop (and the log). |
| S6 | low | Part edges: at stop the MIDI part can be written before the last messages are collected; a note-off or pedal-up that comes after a part ended (a note held past the 60 s limit) starts a new part of its own; All Notes Off / All Sound Off (CC 123/120) and a disconnected keyboard leave notes "held" (part kept open up to 60 s). | At stop: collect the last MIDI before writing the part. Only messages that start sound (note-on, controllers except pedal-up, pitch bend, program, pressure) and loud audio start a part; note-offs and pedal-up only keep it going. CC 120/123 and a disconnect release the notes of that channel/port. Tests (session with stand-ins). |
| S7 | low | Blocks skipped because the input has the wrong channel count (device change while recording) are not on the recorder's timeline, so later part edges shift by them. Already so for normal takes (the block is skipped for all takes, by design). | Known limit, written in the Spec (no code change). |
| S8 | low | Any channel of an armed track's port starts or holds a part, also channels the track does not record (its MIDI channel filter). | Only the channels the armed tracks record count. Test. |
| S9 | low | The overdub hint counts MIDI tracks with a take but without an instrument or MIDI output (they play nothing). | Count them only with an instrument or a MIDI output; T-VST-31. |
| S10 | docs | SPEC-PLAY-05a says the overdub line shows "while stopped" (it shows whenever not recording); SPEC-REC-08 does not mention the 4 s memory, that the transport area scrolls with 5 parts, or Ctrl+R during auto record (does nothing, silently). | Spec texts; Ctrl+R during auto record shows "Auto record is on: stop it first." |
| S11 | tests | T-REC-33 was used twice (fixed in the working tree: the auto record tests are T-REC-34, not yet committed). Untested: MIDI activity details (velocity 0, sustain, a second note-on), gaps in parts mode, a part file that cannot be opened, stop with a queued begin, parts mode with the real disk thread; S1–S4. The session test's "tracks unchanged" comment says more than it checks. | Commit the rename; tests for the listed paths (S1–S4, S6, S8 with them); fix the comment. |
| S12 | docs | README does not mention that the window reopens at its last size and place (`[window] remember`); CLAUDE.md does not say that the Xvfb displays are gone after a while (a Wine test run failed on it). | README sentence; CLAUDE.md note "check `pgrep -a Xvfb` and start the display again". |

## Actions taken (user: "yes fix S1–S12, S1 first")
| # | Done |
|---|------|
| S1 | The MIDI thread takes the job list and the keep point together under `partMutex_`, and keeps from the earliest of it, the open part's start and every pending job's start (`midiKeepFrom`, tested). |
| S2 | Auto record is updated while the window is minimised too. A part never starts further back than 3.5 s (the recorder keeps 4 s): the same start for the WAV and the MIDI files; the cut is logged and shown. Test: playing noticed 7 s late → cut 3.5 s, WAV and MIDI the same length. |
| S3 | Stopped with the stream gone: the open part ends where the audio stopped (recorder start + frames captured; MIDI only: last message + 1 s), with the error "The audio stream stopped during auto record …". Tested. |
| S4 | `AutoPartTracker::abort`: a part that could not get a folder is forgotten with the playing that asked for it; the next playing tries again. Tested (tracker and session: one error per playing). |
| S5 | The recorder marks lost frames in its memory and counts them when a part takes them; loss ranges per part, counted only up to the part's end. The transport shows lost audio frames and MIDI messages while auto record runs; the summary and the log at stop list them (and keyboard disconnects). Recorder test: 100 lost in the remembered part, 0 for a loss cut off at the end. |
| S6 | Stop: the MIDI thread is stopped and the last messages collected before the open part is ended. Only sound-starting messages (and loud audio) start a part; note-offs and pedal-up keep it going. CC 120/123 release a channel's notes, CC 121 lifts the pedal, a disconnect releases the port's notes. Tested. |
| S7 | Written in SPEC-REC-08 as a known limit. |
| S8 | Only the MIDI channels the armed tracks record count (a channel mask per port). Tested. |
| S9 | `overdubTrackCount`: a MIDI take counts only with an instrument or a MIDI output. T-VST-31. |
| S10 | SPEC-PLAY-05a "while not recording"; SPEC-REC-08: memory, scrolling, losses, Ctrl+R; Ctrl+R during auto record says "auto record is on. Stop it first" (cleared at stop). |
| S11 | Auto record tests renamed to T-REC-34 (T-REC-32 and -33 were taken). New tests: S1–S6, S8 as above; recorder: a part file that cannot be made, a part queued just before Stop, the real disk thread (sample-exact). The session test's comment now says what it checks. |
| S12 | README: the window reopens where it was (`[window] remember`). CLAUDE.md: the Xvfb displays and `/dev/shm` disappear; check before GUI or Wine runs. |

Spec v0.33. Linux 159/159, Wine 159/159 (2 runs), the T-REC-34 cases 3 more times on Linux; GUI on Xvfb with the test synth and vkeys (part saved, Ctrl+R message, summary).

# Crosscheck #12 — 2026-11-07 (after P17: Recording / Timeline tabs, timeline of recordings)

Scope: Spec v0.34, Plan, Tests, README, code at commit `d94be3d` (changes since `cc835cd`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Every reviewer finding was checked against the code by Claude (T1–T12 confirmed at the lines named; the reviewer's "Load during auto record" is not a defect: `recording()` is true in auto mode, so Load is disabled then).

| Area | Verdict |
|------|---------|
| Audio thread | ✅ nothing new reaches the callback; preview gains are set on the GUI thread as before |
| UI-11 tabs | ✅ nothing stops on a tab switch, the transport is drawn in one tab at a time (no ID clash) |
| PLAY-07 preview | ❌ Record / Auto record / Return to start during a preview start at the preview's position → **T1**; ⚠️ song position lost **T5**, warnings **T6** |
| PLAY-07 live block | ❌ a normal Record take is not shown while it is recorded → **T2**; ⚠️ the view cannot be scrolled back during a part **T3** |
| PLAY-07 list / layout | ⚠️ cost per frame and per rescan **T4**; daylight saving **T7**; name parsing **T8**; "+60 min" **T9**; overlapping blocks **T10**; empty or unreadable folders **T11** |
| PLAY-07 load | ✅ instruments kept, take start 0, renumbering on a clash, lengths refreshed; ⚠️ allowed during a render **T12** |
| Spec ↔ code ↔ docs | ⚠️ **T13** |
| Spec ↔ tests | ⚠️ **T14** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| T1 | medium | `startPlayback` reads the player's position before `stopPlayback()`; during a preview that is the preview's position. Record then starts the song (and `recordStartPosition`, so the new take's place) in the middle; switching Auto record on restarts the song there; Return to start plays the song, not the recording. | During a preview, the song's own position is used (see T5); Return to start restarts the preview. Spec: Record / Auto record stop a preview. |
| T2 | medium | `recordingsDirty` is set only when a Record take stops, so the take being recorded is not in the list: no growing "REC" block (SPEC-PLAY-07 says there is one) until Refresh or a tab switch. | Set it when a recording starts. T-UI-17 step. |
| T3 | low | While an auto part is being recorded the view is scrolled to the end every frame: earlier parts cannot be looked at during a long part. | Follow only when the view was at the end (or a new recording appeared). |
| T4 | low | Every frame: the whole list (with file paths) is copied, laid out (a string per gap) and every block is a button, also off-screen. Every rescan (tab open, each part begin/end while the tab is shown, Refresh) opens every WAV and parses every MIDI file on the GUI thread: after a day of auto parts a visible stall. | Patch only the live block's length; draw only the visible blocks; rescan by folder (keep the lengths of folders already read, re-read only new ones and the live one). |
| T5 | low | A preview sets the song position to 0 at its start and end: where the user was in the song is lost. | Remember the song position; back to it when the preview stops. |
| T6 | low | Preview: files skipped by the player (other sample rate, unreadable) are only in `playWarnings`, not in the timeline note nor the log. | Log them and show them in the timeline note. |
| T7 | low | Folder times are local clock times treated as one continuous scale: across a daylight-saving change gaps are an hour off and, in autumn, order can be wrong. | Convert with `mktime` (`tm_isdst = -1`) for ordering and gaps; keep the clock label from the name. Test with a fixed time zone where possible. |
| T8 | low | Name parsing: a suffix of 10+ digits overflows `int` (UB); `take-…-` (no digits) is accepted; day 31 is accepted for every month. | Suffix 1–9 digits; day checked against the month. Tests. |
| T9 | low | `gapLabel`: 3570–3599 s gives "+60 min", 86370–86399 s "+24 h 00 min". | Choose the unit from the rounded value. Tests. |
| T10 | low | Overlapping or same-second recordings (`-2` folders) in the same row get the same x: one block hides the other and cannot be clicked; after an overlap later blocks can drift. | Per row: never start before the end of the row's last block. Test. |
| T11 | low | Folders without WAV/MID files are listed as empty minimum-width blocks; a WAV that cannot be read counts 0 s without notice. | Leave out folders without recording files; mark unreadable files in the tooltip. |
| T12 | low | Load onto the tracks is allowed while a render runs (it can replace the take being rendered). | Disable it during a render, as Record. |
| T13 | docs | README still says "steps P2–P16". SPEC-PLAY-07 does not say what Record / Auto record / Return to start do during a preview, nor that the song position comes back after it. | README P2–P17; Spec sentences with T1/T5. |
| T14 | tests | T-UI-17 lacks a Record take shown live and Record during a preview; unit tests lack overlaps, `-N` folders in `listRecordings`, gap-label edges, bad suffixes, unreadable WAV, a number taken by a track of the other kind. | Add them with T1–T12. |

## Actions taken (user: "yes fix T1–T14, T1 first, and pin the dependencies")
| # | Done |
|---|------|
| T1 | `startPlayback` stops a preview before it reads the position, so Record (and its take placement) uses the song's position; switching Auto record on stops a preview without starting the song; Home during a preview plays the recording again from its start. GUI: after a 5 s preview with the song at 0, Record started "from 0.000 s". |
| T2 | A Record start marks the list for reading again. The scan leaves empty folders out (T11) and a MIDI-only take has no file until it ends, so the folder being recorded (take or auto part) is added to the list while it is recorded. GUI: "16:47 0:03 REC" in the Record takes row. |
| T3 | The view follows a growing recording only when it was at the end in the frame before. |
| T4 | No per-frame copy (only the live block's length is updated in place); the layout is cached and laid out again only when the list, the zoom or the text size changes, or a recording grows; only blocks and pause markers in view are drawn; a rescan reuses every folder whose file names and sizes are unchanged (tested). |
| T5 | A preview remembers the song position and `stopPlayback` goes back to it. GUI: song stopped at 2 s, a preview played to its end, Record then started "from 2.069 s". |
| T6 | Files the players skip are logged ("Timeline: …") and shown under the timeline. |
| T7 | `RecordingInfo::start` is real time (`continuousTime`: `mktime` with `tm_isdst = -1`), `clock` keeps the name's clock time for the labels. Test (Linux, CET rules): 01:30 → 03:30 is 3 h in October, 1 h in March. |
| T8 | Suffix 1–9 digits; the day is checked against the month (leap years). Tests. |
| T9 | `gapLabel` picks the unit by the rounded value ("+1 h 00 min", "+1 min", "+1 day"). Tests; the existing "+60 min" expectation was this defect and now reads "+1 h 00 min". |
| T10 | Per row, a block starts at or after the end of the row's block before it; the drawn end of both rows is kept. Test; GUI: the `-2` part is beside the other. |
| T11 | Folders without .wav/.mid files are left out (not the one being recorded, see T2); a WAV that cannot be read is listed in `problems` and in the tooltip. Tests. |
| T12 | Load onto the tracks is disabled during a render (and refused in `loadRecording`). |
| T13 | README "P2–P17"; SPEC-PLAY-07: preview with Record / Auto record / Home, song position kept, the take being recorded, empty folders, daylight saving. |
| T14 | New test cases: same-second blocks; empty folders, unreadable files, `-N` folders and reading again; daylight saving (Linux); more names and gap labels. T-UI-17 extended (preview + Record, Home, Auto record, scrolling). The reviewer's "number taken by a track of the other kind" was already tested (`03-Odd.mid`). |
| Pins | `cmake/Dependencies.cmake`: every library pinned to the commit hash of its release with the release as a comment (no `GIT_SHALLOW`: a shallow clone needs a tag name); `tools/check_licenses.py` requires the hash and compares the comment with `THIRD_PARTY.md` (caught a tag-only copy). Updated to the newest releases: RtMidi 6.1.0 (classes now in `rt::midi`: our forward declaration changed), Dear ImGui v1.93.0, GLFW 3.5.1; the others were already the newest. SPEC-CI-04, T-CI-03. GitHub Actions were already pinned and the newest (checkout v7.0.1, cache v6.1.0, upload-artifact v7.0.2). |

Spec v0.35. Linux 167/167, Wine 166/166 (the daylight-saving case is Linux-only); GUI on Xvfb as above.

# Crosscheck #13 — 2026-11-07 (after P18: Audio setup tab, tab order; text size at once)

Scope: Spec v0.36, Plan, Tests, README, Demo-checklist, code at commit `cc9a41d` (changes since `572d2b5`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions; it read the ImGui v1.93.0 sources for the internal API used). Every reviewer finding was checked against the code by Claude (U1–U8 confirmed at the lines named).

| Area | Verdict |
|------|---------|
| Audio thread | ✅ nothing in the diff reaches the callback; driver messages are taken on the GUI thread only |
| UI-12 tab order | ✅ drag and Settings → Tabs… can neither lose nor repeat a tab; saved only when changed; no feedback loop with ImGui's own order |
| UI-12 last tab | ⚠️ the first frame of a new tab bar shows the first tab and saves it as `last` → **U2** |
| UI-12 keyboard | ❌ the Spec says the Tabs dialog is usable with the keyboard; keyboard navigation is not on in ImGui → **U1**, **U4** |
| UI-12 status line | ✅ stopped / stalled / notes with the button, dropouts always shown; ⚠️ **U3**, **U5**, **U8** |
| UI-08 text size | ✅ `_NextFrameFontSizeBase` is applied at the next `NewFrame` (imgui.cpp `UpdateFontsNewFrame`), also with display scaling |
| Config `[tabs]` | ✅ case, spaces, repeats, unknown names, unknown `last`, round trip; ⚠️ tests **U6** |
| Spec ↔ code ↔ docs | ⚠️ **U7** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| U1 | medium | `ImGuiConfigFlags_NavEnableKeyboard` is never set, so Tab / arrows / Enter / Space do nothing in any window; SPEC-UI-12 promises a Tabs dialog usable with the keyboard (and the global rules ask for accessible GUIs). | Turn keyboard navigation on (after `CreateContext`); check that Space / Enter shortcuts of the transport do not clash with activating a focused button. GUI test. |
| U2 | low | On the first frame of a tab bar (at start, and after Settings → Tabs… makes a new one) ImGui shows the first tab's content before `SetSelected` takes effect: one frame of the wrong tab (with a timeline rescan if Timeline is first) and `last` saved twice (wrong for a moment on disk). | Do not change `last` while a tab is being selected (`requested` not empty and not this tab); skip the content of that frame. |
| U3 | low | Stalled stream and notes at the same time: two "Audio setup" buttons with the same ID (ImGui's duplicate-ID warning). | Different IDs (`##stalled`, `##notes`). |
| U4 | low | Tabs dialog: rows keyed by index, so after Up the keyboard focus stays on the row, not on the tab that moved; only matters with U1. | Key the rows by tab id. |
| U5 | low | The Recording tab's stalled line leaves out "Try Settings > Audio devices > Apply." that the Audio setup tab has. | Add it. |
| U6 | tests | T-UI-18 unit tests lack: empty `order`, only unknown names, case / spaces in `last`, `[Tabs]` / `ORDER` key case, re-saving a changed order over an existing one. | Add them. |
| U7 | docs | SPEC-UI-12: an unknown `last` is not "left out" but replaced by `recording` (with a warning). SPEC-REC-01a: "Input levels" does not say it is now on the Audio setup tab. T-UI-18 expected "2 warnings" is right but does not say repeats give none. | Spec / Tests sentences. |
| U8 | low | `audioWarnings` grows without limit (cleared only on Apply); a driver that keeps sending messages grows the list, the Recording line's "N note(s)" and the Audio setup tab. Existed before P18; now polled on every tab. | Fold repeats ("… (×12)") and keep at most e.g. 50, with a "more in the log" line; the log keeps every message (losses reported). |

## Actions taken (user: "yes fix U1–U8, U1 first")
| # | Done |
|---|------|
| U1 | Keyboard navigation on (`ImGuiConfigFlags_NavEnableKeyboard`). ImGui's navigation uses Space (press the control in focus) and Home (scroll to the top) only when no one owns those keys, so mini-daw owns them every frame (`claimTransportKeys`): Space stays Play / Stop, Home to the start; text fields still get a typed space and Home. SPEC-UI-13, T-UI-20. GUI: focus frame with Tab; Space did not press the focused button, Enter did; a space and Home in a track name. |
| U2 | While a tab is being selected (at start, after the Tabs dialog, the Audio setup button) the tab ImGui shows meanwhile is not drawn and not saved as the last one. GUI: start with `last = audio`, `config.ini` not written. |
| U3 | The status line's buttons have their own IDs (`##stopped`, `##stalled`, `##notes`). |
| U4 | Tabs dialog rows keyed by tab; at the top or bottom the focus goes to the moved tab's other button. The GUI test found that a Down press then acted twice (the moved row came again in the same frame): the move is now made after all rows are drawn. GUI: four Enter presses, one place each. |
| U5 | The Recording tab's stalled line says "Try Settings > Audio devices > Apply." |
| U6 | Tests: empty order, only unknown names, case and spaces in section, key and `last`, repeats without a warning, saving again over an existing order (one `order` line). |
| U7 | SPEC-UI-12: unknown `last`, repeats, focus in the dialog, notes; SPEC-REC-01a: Input levels on the Audio setup tab; T-UI-18 expectations. Plan PX row brought up to date (#11–#13). |
| U8 | `addNote`: a repeated driver message is counted ("… (3 times)"), at most 50 notes, then "More notes: see View > Log."; every message stays in the log. T-UI-19. |

Spec v0.37. Linux 169/169, Wine 168/168 (the daylight-saving case is Linux-only); GUI on Xvfb as above.
