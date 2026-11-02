# Test cases — mini-daw

Type: **A** = automated unit/integration test, **M** = manual test.
Status column in the Results section at the end.

## UI / contrast
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-UI-01 | SPEC-UI-01 | M | Start the app with no config file | White background, black text |
| T-UI-02 | SPEC-UI-02 | A | Compute the contrast ratio of every colour pair in the default theme | All ≥ 4.5:1 |
| T-UI-03 | SPEC-UI-03 | M | Switch the preset AA → AAA → Max at runtime | Colours update right away; all pairs ≥ 7:1, then 21:1 |
| T-UI-04 | SPEC-UI-04 | A | Set `min_contrast = 7.0` in `[ui]` of the config file, then start the app | All pairs ≥ 7:1 |
| T-UI-05 | SPEC-UI-05 | A | Set a custom minimum of 3.0 | Rejected with a message; 4.5:1 stays in effect |
| T-UI-06 | SPEC-UI-05 | A | Set a custom minimum of 10.0 | All pairs ≥ 10:1 |
| T-UI-07 | SPEC-UI-06 | A | Theme has a pair at 5:1, minimum set to AAA | That pair is adjusted to ≥ 7:1 |
| T-UI-08 | SPEC-UI-02 | A | Contrast function on known pairs: #000/#FFF, #777/#FFF | 21.0, about 4.48 |
| T-UI-09 | SPEC-UI-07 | M | Look at the level meters with the colours ignored | Level can still be read (text value or pattern) |
| T-UI-10 | SPEC-TECH-05 | M | Start the GUI on Windows without a GPU/OpenGL driver (VM), and with `MINIDAW_D3D_WARP=1` | Window opens; status line says "Direct3D 11 (WARP software renderer)" |
| T-UI-11 | SPEC-TECH-05 | M | Make every renderer fail (e.g. Windows Basic Display with D3D disabled) | A message box explains the problem; no silent exit |

## Devices
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-DEV-01 | SPEC-DEV-01 | M | Open device settings with a USB audio interface and a MIDI keyboard connected | Both appear in the lists |
| T-DEV-02 | SPEC-DEV-02 | M | Select 44.1 kHz and a 128-sample buffer | Stream runs at those settings |
| T-DEV-03 | SPEC-DEV-03 | M | Restart the app | The previous device and settings are restored |
| T-DEV-05 | SPEC-DEV-01 | A | `scanDevices()` on a machine without audio/MIDI hardware | Does not throw; problems are returned as warnings |
| T-DEV-04 | SPEC-DEV-03 | A+M | Restart after the saved device has been unplugged | Clear message; falls back to the default device; no crash; saved choice kept |
| T-DEV-06 | SPEC-DEV-05 | M | Start with a driver that fails on its own thread (e.g. WASAPI under Wine) | Error with the driver's message; not shown as running |
| T-DEV-07 | SPEC-DEV-06 | M | `mini-daw --audio-test 5` on Windows 10 with Line In + Speakers at 48 kHz | Runs; callbacks ≈ expected; 0 dropouts; peak levels change when a signal is applied |

## Recording
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-REC-01 | SPEC-REC-01/02 | M | Arm 2 audio tracks and 1 MIDI track, record 30 s (Windows: USB keyboard or virtual port; Linux VM: virtual keyboard) | 2 WAV files and 1 MIDI file, all the same length |
| T-REC-02 | SPEC-REC-03 | A | WAV writer: write a known sine wave, read it back | Header valid (24-bit, correct rate); samples match |
| T-REC-03 | SPEC-REC-04 | A | SMF writer: write known events, parse them back | Type 1 file; events and timing match |
| T-REC-04 | SPEC-REC-04 | M | Loop audio out to audio in, send MIDI notes that also trigger a click, record both | MIDI note-on and audio click within 1 buffer of each other |
| T-REC-05 | SPEC-REC-05, NF-01 | M | Record 4 tracks for 60 minutes | No dropouts or xruns logged; files complete |
| T-REC-06 | SPEC-REC-05 | A | Ring buffer stress test: producer and consumer threads, 10⁸ samples | No lost or duplicated samples |
| T-REC-07 | SPEC-REC-05a | A | Write a WAV file, refresh the header, write more, read the file before closing | File is valid and holds the audio up to the header refresh; after closing, all of it |
| T-REC-08 | SPEC-REC-03, 05, NF-01 | A | Recorder with 4 input channels and 2 takes (mono ch 3; stereo ch 4+1), a test thread plays the audio callback for 5 s, the real disk thread writes | Both files hold every frame, sample-exact, in the right channel order; 0 lost frames |
| T-REC-09 | SPEC-REC-05b | A | Recorder without a disk thread and a 4096-frame ring; feed 10 000 frames | 6000 frames reported lost; the 4000 buffered frames are written on stop, followed by 6000 frames of silence (file length 10 000) |
| T-REC-10 | SPEC-REC-06 | M | `mini-daw --record-test 10 <folder>` on Windows 10 with a working input, while speaking or playing into it | A 10 s, 24-bit WAV in the folder that plays back what was heard; 0 lost frames, 0 dropouts (xruns); exit code 0 |
| T-REC-11 | SPEC-REC-05c | A | WAV writer placed just below the 4 GiB limit (test hook); write up to it, then one frame more | Write up to the limit succeeds; the next is refused with a "4 GiB limit" message; size fields fit in 32 bits incl. padding |
| T-REC-12 | SPEC-REC-05b | A | WAV writer: a write fails halfway through a block (test hook), then close | Later writes refused; file cut back to the last complete write, padding byte added, valid header |
| T-REC-13 | SPEC-REC-02, 05b | A | Recorder with a mono and a stereo take; 1000 frames lost in the middle, then more audio | Both files 7000 frames; lost part is silence; audio after it is at its original position in both takes; 1000 lost frames reported per take |
| T-REC-14 | SPEC-REC-05b | A | Recorder: the disk "fills up" (test hook) after 1000 frames, 4000 more frames arrive | Disk error reported; 4000 lost frames (live and in the result); file valid with 1000 frames |
| T-REC-15 | SPEC-REC-05a | A | `sync_to_disk` in `[recording]`: missing, `false`, `Off`, `yes`, `maybe`; save and reload | Default on; values parsed; `maybe` gives a warning and stays on; saved value survives reload. T-REC-07 also covers a header refresh with the hard sync |
| T-REC-16 | SPEC-REC-04b | A | SMF writer, `ppq` and `smpte`: known events at known frames (48 kHz), read back | Type 1, 2 tracks, division 960 or SMPTE −25/40; ticks = frames × 1920/48000 or × 1000/48000 (rounded); tempo event only with `ppq`; end-of-track present |
| T-REC-17 | SPEC-REC-04b | A | MIDI take builder: events on channels 1 and 2, track filter channel 2; events before the start; a note held at the end | Only channel 2 kept; early events dropped; times relative to the start; held note gets a note-off at the end frame |
| T-REC-18 | SPEC-REC-04a | A | Clock → frame mapping with a known (frame, time) pair at 48 kHz; clock smoother with 0–15 ms callback jitter and a start-up burst; 300 ppm clock drift with a 100 ms dropout | +10 ms maps to +480 frames; seqlock reader never sees a half-written pair; smoothed time within 2 ms of ideal after 1.5 s; drift followed within 1 ms, restart after the dropout |
| T-REC-19 | SPEC-REC-07, 01a | A | Take folder and file names: track names with `/ \ : * ? " < > |`, duplicate folder name in the same second; default tracks for 1 and 2 inputs | Safe names; second folder gets `-2`; one mono or one stereo track |
| T-REC-20 | SPEC-REC-04c | A | Rewrite a MIDI take while recording, then read it | Valid file with the events so far; no `.tmp` left after stop |
| T-REC-21 | SPEC-REC-02a | M | GUI: arm 1 audio + 1 MIDI track, Ctrl+R, play, Ctrl+R | Take folder with one WAV and one MID of the same length; elapsed time shown while recording; "armed" visible as text |
| T-REC-22 | SPEC-REC-07 | A+M | `[recording] folder`, `midi_timing`, `sync_to_disk` in the config; Settings → Recording…; hand-written Windows paths (`"D:\Takes\"`, `\\server\takes` without quotes) | Parsed with warnings for bad values; paths kept as typed; changes saved and read back unchanged |
| T-REC-23 | SPEC-REC-01, 02, 04, 04a, 07 | A | Session with stand-in audio/MIDI: 1 audio + 2 MIDI tracks (all channels; channel 2) on one port, 1 unarmed track; MIDI before the first audio frame, a held note | One WAV (4800 frames) and two MID files named `NN-name`; early message dropped; positions in ticks exact; channel filter; held note ends at the take end; no `.tmp` left |
| T-REC-24 | SPEC-REC-04a | A | MIDI-only session | Timeline from the stream clock: positions and end of track exact |
| T-REC-25 | SPEC-REC-01a, 07 | A | Session start failures: nothing armed, MIDI track without port, port not open, stream not running, no audio input, channel that does not exist | Clear message each time; no take folder left behind |
| T-REC-26 | SPEC-REC-02, 04, 04c, 05b | A | Session robustness: first audio take stops on a disk error; Stop before any audio; MIDI message that cannot be placed; MIDI file rewrite blocked, then free; stream gone before Stop | MIDI keeps the full timeline; "No audio arrived"; counted as lost (live and in the result); "retrying" then cleared; last periodic MIDI file kept |
| T-REC-27 | SPEC-REC-04b | A | MIDI take builder edge cases: note-off without note-on, a note started twice, empty or reversed range, unsorted input, short message | Kept; two note-offs; nothing; sorted (stable); ignored |
| T-REC-28 | SPEC-REC-07 | A | Track and folder names with non-ASCII characters ("Gitaar éè ♪", "Opnamen ü") | Files and folders get exactly these names (UTF-8 → path conversion; matters on Windows) |
| T-REC-29 | SPEC-REC-01a, 02a | M | GUI: arm a MIDI track whose device is unplugged, plug it in; also add tracks while the audio stream is not running, then start it | The port opens within about 2 s (or at Record) without re-arming; the user's tracks stay, no default track is added |
| T-REC-30 | SPEC-REC-01b | A | Session: a MIDI port is unplugged during a take (and once before it) | Take result: 1 disconnect for that port, 0 for the other; events that arrived are kept; take counts as lost data |
| T-REC-31 | SPEC-REC-01b | A | Port matching: same name, new ALSA client number, Windows renumbering, missing port, two devices of the same kind | Found; found; found; not found; only the exact name |
| T-REC-32 | SPEC-REC-01b | M | Arm a MIDI track; unplug the keyboard, plug it in again; also during a recording; plug in a new keyboard | "DISCONNECTED - waiting for it", then back to the message count without re-arming; take summary reports the disconnect; the new keyboard appears in the port list within about 2 s |

## Diagnostics
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-LOG-01 | SPEC-LOG-01 | A | Open the log three times | New file each run with time + level + text, line breaks indented; the previous run is in `mini-daw.old.log` |
| T-LOG-02 | SPEC-LOG-01 | A | Lines in memory, counts, more than 2000 lines, two threads writing | Order kept, oldest dropped, counts right, nothing lost between threads |
| T-LOG-03 | SPEC-LOG-01 | A | The log file cannot be created | Error text returned; the log still works in memory |
| T-LOG-04 | SPEC-LOG-01 | M | GUI: View → Log (Ctrl+L), "Only warnings and errors", "Copy all", "Open log folder"; unplug a MIDI keyboard | Lines shown with level as text; filter works; clipboard holds the lines; the file manager opens the folder; the disconnect appears in the log and the status line count |

## Monitoring
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-MON-01 | SPEC-MON-01, 01a | M | Monitoring on, then off, on an armed track (GUI, or `--audio-test 5 --monitor`) | Input is heard and the output meter shows it, then silent; state shown as text; delay note shown while on |
| T-MON-02 | SPEC-MON-02, 02a | A+M | Feed a −6 dBFS sine wave | Meter reads −6 dB ± 0.5 |
| T-MON-03 | SPEC-MON-01a | A | Monitor gains from tracks: mono, stereo, not armed, monitor off, channel used twice, channel beyond the device, MIDI track | Mono both sides, stereo left/right, the rest silent, a shared channel heard once |
| T-MON-04 | SPEC-MON-01a, NF-01 | A | Mixer: steady gains, switch on, switch off, mono output, overload, no input | Routed as set; gains ramp over the block; L/R averaged; output limited to ±1 with the real peak reported; silence |
| T-MON-05 | SPEC-MON-02a, UI-07 | A | Meter: fall rate, peak hold, text window, clip at full scale, clear, NaN input | 20 dB/s; hold 1.5 s then falls; louder peak at once; CLIP latched until cleared; NaN = silence |
| T-MON-06 | SPEC-MON-01a, 02a | M | Windows (WASAPI): headphones, an armed track with Monitor on; sing or play loudly up to clipping; click CLIP; press Record | Input heard with little delay; CLIP appears and clears on click and at Record; no clicks when switching Monitor on/off |

## Playback
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-PLAY-01 | SPEC-PLAY-01 | M | Play / Stop / Record / return to start | Transport state and position behave correctly |
| T-PLAY-02 | SPEC-PLAY-02 | M | Play back 2 recorded audio tracks | In sync, with no drift after 10 min |
| T-PLAY-03 | SPEC-PLAY-03 | M | Play back a MIDI take to an external MIDI output | Notes heard with correct timing |
| T-PLAY-04 | SPEC-PLAY-04 | M | Mute track 1, solo track 2 | Only the expected tracks are heard |

## VST3 (phase 2)
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-VST-01 | SPEC-VST-01 | M | Scan with a known free VST3 instrument installed | It appears in the list |
| T-VST-02 | SPEC-VST-02 | M | Load the instrument on a MIDI track and play the keyboard | Sound is heard live |
| T-VST-03 | SPEC-VST-03 | M | Record while playing the instrument | WAV take contains the instrument audio |
| T-VST-04 | SPEC-VST-04 | M/A | Render a recorded MIDI take offline | WAV has the expected length; notes at the expected positions |
| T-VST-05 | SPEC-VST-05 | M | Open and close the plugin editor | Window opens and closes; no crash |
| T-VST-06 | SPEC-VST-06 | M | Change a preset, save the project, reopen it | Plugin state restored |
| T-VST-07 | SPEC-NF-03 | M | Plugin crashes during recording | Takes already written are intact and playable |

## Non-functional
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-NF-02 | SPEC-NF-02 | M | Loopback cable from output to input; play a click and record it with WASAPI exclusive mode at 256 samples, 48 kHz | Measured round-trip latency ≤ about 20 ms |

## Project
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-PRJ-01 | SPEC-PRJ-01/02 | A | Save a project with 3 tracks, reopen it | Tracks, takes and settings identical |
| T-PRJ-02 | SPEC-PRJ-02 | A | Open a project with a missing take file | Clear message; the rest loads |

## Text size
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-UI-12 | SPEC-UI-08 | A+M | `font_size = 20` in config; Ctrl+= / Ctrl+- / Ctrl+0 | Text 20 px; changes by 2 px, limited to 12–32; saved; bad values give a warning |

## Licensing
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-LIC-01 | SPEC-LIC-01 | A | `tools/check_licenses.py` checks the `LICENSE` file at the repository root | MIT text with "Copyright (c) 2026 Maarten Boot" |
| T-LIC-02 | SPEC-LIC-02 | A | CI script checks that every dependency in the build has an entry in `THIRD_PARTY.md` with an allowed licence (MIT, BSD, zlib, Boost, Apache-2.0) | Passes; fails if an entry is missing or the licence is not allowed |
| T-LIC-03 | SPEC-LIC-03 | A | Default configure and build (no options set) | Builds; no ASIO symbols or headers involved; repository contains no ASIO SDK files |
| T-LIC-04 | SPEC-LIC-03 | M | Configure with `-DMINIDAW_WITH_ASIO=ON` and no SDK path set | Configure stops with a clear message explaining where to get the SDK and what its licence means |
| T-LIC-05 | SPEC-LIC-04 | M | Look at the product name, build names, About box and installer, with and without ASIO enabled | No "ASIO" in any name; logo, if any, only in ASIO builds and only in the About box, splash screen or docs |

## Build and CI
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-CI-01 | SPEC-CI-01 | A | `tools/check_licenses.py` scans `.github/workflows/*.yml` | Every `uses:` is pinned to a 40-character SHA; fails otherwise |
| T-CI-02 | SPEC-CI-02 | A | Push to GitHub | Windows, Linux and licence jobs all pass; both smoke runs succeed; both program downloads are present |
| T-CI-03 | SPEC-CI-03 | M | Cross-compile with the MinGW toolchain file | `mini-daw.exe` and `minidaw_tests.exe` build without errors |


## Results
| Date | Test | Environment | Result | Notes |
|------|------|-------------|--------|-------|
| 2026-11-01 | T-LIC-02 | Linux | pass | Also checked against planted failures: wrong version, GPL licence, unpinned action, `asio.h` file. All 4 caught. |
| 2026-11-01 | T-LIC-03 | Linux | pass | No ASIO SDK files; default build has no ASIO |
| 2026-11-01 | T-LIC-04 | Linux + MinGW | pass | Clear message on Linux ("Windows only") and on Windows without an SDK path |
| 2026-11-01 | T-LIC-05 | Linux | pass (partial) | Product name checked by unit test; About box, installer and logo come later |
| 2026-11-01 | T-CI-01 | Linux | pass | |
| 2026-11-01 | T-CI-02 | — | not run | No GitHub remote yet |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41337403277 | **FAIL** (Windows) | Linux and licence jobs pass. Windows configure: "could not find any instance of Visual Studio" for `-G \"Visual Studio 17 2022\"` on `windows-2025` → generator removed |
| 2026-11-01 | T-CI-03 | Linux (MinGW 13) | pass | Not run under Wine: root disk 98% full |
| 2026-11-01 | T-DEV-05 | Linux VM (no sound card) | pass | ALSA sequencer access denied → returned as MIDI warnings |
| 2026-11-01 | T-UI-01 | Linux (unit test + Xvfb screenshot) | pass | White background, black text |
| 2026-11-01 | T-UI-02 | Linux (unit test) | pass | 19 pairs; lowest 5.38:1 (accent on pressed control) |
| 2026-11-01 | T-UI-03 | Linux (Xvfb + xdotool) | pass | Ctrl+Shift+Up twice → Max; screen pure black/white; saved to config. Menu checked by screenshot |
| 2026-11-01 | T-UI-04 | Linux (unit test + `--contrast-report`) | pass | Also: user comments kept when saving |
| 2026-11-01 | T-UI-05 | Linux (unit test + `--contrast-report`) | pass | Value 3 → warning, AA used |
| 2026-11-01 | T-UI-06 | Linux (unit test) | pass | 10:1, 15:1 also checked |
| 2026-11-01 | T-UI-07 | Linux (unit test) | pass | |
| 2026-11-01 | T-UI-08 | Linux (unit test) | pass | #777/#FFF = 4.48, #767676 = 4.54, #595959 = 7.00 |
| 2026-11-01 | T-CI-03 | Linux (MinGW 13) | pass | P3 build; exe needs only Windows system DLLs (see Spec §8 about MFPlat) |
| 2026-11-01 | T-DEV-05 | **Windows 10, real machine** (user) | pass | WASAPI: Speakers (out 2, default, 48 kHz), Line In (in 2, default, 44.1 kHz). MIDI: 0 inputs, 1 output (Microsoft GS Wavetable Synth). |
| 2026-11-01 | T-CI-03 | **Windows 10, real machine** (user) | pass | MinGW-built exe starts and runs `--list-devices` without extra DLLs. Finding: RtMidi printed "no MIDI input devices" straight to the console → fixed (captured; "no devices" notices dropped). |
| 2026-11-01 | T-UI-01 | **Windows 10, real machine** (user) | **FAIL** | `GLFW error 65542: WGL: The driver does not appear to support OpenGL`. No window. → Windows renderer switched to Direct3D 11 + WARP |
| 2026-11-01 | T-UI-10 | Wine 10 on Xvfb (D3D11 hardware path, and WARP forced) | pass | Both paths open the window and show the correct renderer name. Wine is not real Windows; needs re-test by user |
| 2026-11-01 | T-CI-03 | Wine 10 | pass | `minidaw_tests.exe`: 29 tests, 227 assertions pass |
| 2026-11-01 | T-UI-01 | **Windows 10, real machine** (user) | pass | "looks good now" after the switch to Direct3D 11 |
| 2026-11-01 | T-UI-10 | **Windows 10, real machine** (user) | pass | GUI opens on the machine without OpenGL. Which D3D path (hardware/WARP) not yet reported |
| 2026-11-01 | T-DEV-02 | Linux VM (ALSA→PulseAudio) | pass | `--audio-test 2`: 48 kHz, 256 frames, 377 callbacks (≈375 expected), 0 dropouts |
| 2026-11-01 | T-DEV-03 | Linux (unit tests + Xvfb) | pass | Settings saved by name, incl. quotes/semicolons; reloaded identically |
| 2026-11-01 | T-DEV-04 | Linux (unit test + Xvfb) | pass | "Gone Device" → note, PulseAudio used, saved choice kept |
| 2026-11-01 | T-DEV-06 | Wine 10 | pass | WASAPI fails under Wine; now reported as error with driver text (before the fix it was shown as running) |
| 2026-11-01 | T-DEV-07 | — | not run | Needs the user's Windows machine |
| 2026-11-01 | T-UI-12 | Linux (unit tests + Xvfb + xdotool) | pass | 20 px from config; Ctrl+= → 22 px, saved |
| 2026-11-01 | T-CI-03 | Wine 10 | pass | `minidaw_tests.exe`: 42 tests, 264 assertions |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41337681818 | pass | windows (Visual Studio 18 2026, MSVC 19.51) 3m14s, 43/43 tests; linux 1m5s; licence. Only 2 MSVC warnings (C4996 `getenv`) → fixed. Smoke run on the runner (no sound card): RtAudio printed "No devices found" to the console → now captured |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41341970036 | pass | No MSVC warnings left; `--version` clean. Remaining: "No devices found" reached the GUI/CLI as a warning via RtAudio's error callback → filtered there too |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41349876675 | pass | windows 3m28s, linux 1m28s, licence; 43/43 tests; smoke run on the runner is now free of warnings |
| 2026-11-01 | T-DEV-07 | Windows VM, no audio input active | blocked | Error reported correctly: "no audio arrived within 1 second. Driver: RtApiWasapi::wasapiThread: Unable to initialize capture audio client." Next: output-only run (`input_device = (none)`), then a run on a machine with a working input |
| 2026-11-01 | T-DEV-07 (output only) | Windows VM, `input_device = (none)` | pass | WASAPI, Speakers 48 kHz, 2 ch; 1044 callbacks (expected 938), 0 xruns. The excess was a flaw in the test (count started at open, `sleep_for` overshoot) → now measures real time from a fixed start; Linux: 376 in 2.00 s (expected 375) |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41351279135 | pass | licence 4s, windows 3m15s, linux 1m11s; both downloads present (`mini-daw-windows-x64`, `mini-daw-linux-x64`). Linux smoke run works; ALSA prints its own "cannot find card '0'" lines on the runner (no sound card) — they bypass our `std::cerr` capture |
| 2026-11-01 | — (console noise, SPEC-DEV-01 output) | Linux, `ALSA_CONFIG_PATH=/nonexistent` | pass | Before: 8 "ALSA lib …" lines on `--list-devices`; after the silent `snd_lib_error_set_handler`: 0 |
| 2026-11-01 | T-REC-02 | Linux + Wine (MinGW build) | pass | Sine read back within one 24-bit step; header fields; odd-size padding; clipping/NaN; 16-bit and float reading |
| 2026-11-01 | T-REC-06 | Linux + Wine | pass | 10⁸ values with changing block sizes, 0 errors (≈0.1 s). First run hung: buffer overrun in the test's producer (blocks up to 515 values in a 512 buffer) → fixed |
| 2026-11-01 | T-REC-07 | Linux + Wine | pass | |
| 2026-11-01 | T-REC-08 | Linux + Wine | pass | 937 blocks of 256 frames into 2 takes; 0 mismatches, 0 lost |
| 2026-11-01 | T-REC-09 | Linux + Wine | pass | |
| 2026-11-01 | T-REC-10 | Linux VM (PulseAudio, no real input) | pass (Linux only) | 2 s → 96 256 frames, 24-bit stereo 48 kHz (checked with Python `wave`); 0 lost, 0 xruns. Windows: to do by the user |
| 2026-11-01 | all automated | Linux 58/58 ctest; Wine 57/57 Catch2 cases | pass | |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41354090755 | pass | windows 2m55s (58/58), linux 1m28s (58/58), licence; MSVC C4324 on `SpscRing` (intended padding) → switched off for that class |
| 2026-11-01 | T-CI-02 | GitHub Actions run 41354630677 | pass | windows 3m11s, linux 1m35s, licence; 58/58 on both; no MSVC warnings |
| 2026-11-01 | crosscheck #3 fixes (H1–H9) | Linux 65/65 ctest; MinGW build under Wine 64/64 Catch2 cases (669 assertions) | pass | New: T-REC-11..15; T-REC-09 now expects the silence; T-REC-07 also with hard sync |
| 2026-11-01 | T-REC-10 | Linux VM (PulseAudio, no real input) | pass (Linux only) | 2 s with `sync_to_disk` on: 96 256 frames, 24-bit stereo 48 kHz; 1 s with it off: 48 128 frames; 0 lost, 0 dropouts, exit code 0. New output wording "lost frames" / "dropouts (xruns)" |
| 2026-11-01 | T-REC-07 | GitHub Actions run 41357305545 (windows) | **FAIL** | Both T-REC-07 cases: the take could not be read while being written (`_wfopen_s` opens without sharing). Linux, licence pass. Wine did not show it → `_wfsopen(..., _SH_DENYWR)` |
| 2026-11-02 | T-CI-02 | GitHub Actions run 41432521646 | pass | windows 4m29s (65/65, incl. T-REC-07 on real Windows), linux 1m28s (65/65), licence; no MSVC warnings; both downloads present |
| 2026-11-02 | T-REC-16..20, T-REC-22 | Linux 76/76 ctest; MinGW build under Wine 75/75 Catch2 cases (783 assertions) | pass | SMF ppq/smpte round trip, take builder, clock mapping + seqlock stress (2·10⁶ writes, 0 torn reads), take names, config incl. Windows paths with backslashes |
| 2026-11-02 | T-REC-21 (audio part) | Linux VM, Xvfb + xdotool | pass (partial) | Arm, Ctrl+R, 2.5 s, Ctrl+R: take folder with `01-Audio 1+2.wav` (128 000 frames), elapsed time shown, "armed" as text, widgets disabled while recording. MIDI part needs sequencer access (`audio` group) |
| 2026-11-02 | T-REC-10 | Linux VM (PulseAudio) | pass (Linux only) | `--record-test 2` through the session: take folder, 97 280 frames, 0 lost, 0 dropouts; MIDI scan warning printed (no sequencer access) |
| 2026-11-02 | T-CI-02 | GitHub Actions run 41470756957 | pass | P6a/P6b: windows 3m7s (76/76, MSVC, no compiler warnings), linux 2m0s (76/76), licence; both downloads present |
| 2026-11-02 | T-REC-01 (Linux part) | Linux VM, ALSA "Midi Through" port, notes sent with `aplaymidi` (10 notes 500 ms apart, 100 ms long, + 1 controller) | pass | `--record-test 8`: WAV + MID in one take folder, 21/21 MIDI events, 0 lost; MIDI end of track = audio length (8.00 s) |
| 2026-11-02 | T-REC-04 (timing spread only) | same, 256-frame buffer (5.3 ms) | pass | Note gaps 499.5–501.0 ms (max error 1.0 ms), note lengths within 1.0 ms. Absolute offset to audio not measured (needs loopback of a click) |
| 2026-11-02 | T-REC-04 (timing spread only) | same, GUI, 1024-frame buffer (21.3 ms) | pass, but see note | Gaps 490.6–509.4 ms (max error 9.4 ms), one note 79 ms instead of 100: within one buffer as specified, but the spread grows with the buffer size → clock smoothing proposed |
| 2026-11-02 | T-REC-16 (live) | same, `midi_timing = smpte` | pass | Division 0xE728 (−25 fps × 40); gaps within 2 ms |
| 2026-11-02 | T-REC-17 (live) | same, one 4 s note, recording stopped after 3 s | pass | Note held at Stop gets its note-off at the end of the take (2987 ms) |
| 2026-11-02 | T-REC-21 | Linux VM, Xvfb + xdotool, sequencer via `sg audio` | pass | Audio + MIDI track armed, Ctrl+R, notes sent, Ctrl+R: `01-Audio 1+2.wav` (362 496 frames) and `02-MIDI 2.mid` (21 events); MIDI activity and message count shown while recording |
| 2026-11-02 | T-REC-18 + T-REC-04 (spread) after clock smoothing | Linux 78/78; Wine 77/77 Catch2 cases; live on the VM with `aplaymidi` → Midi Through | pass | 1024-frame buffer: gaps within 0.52 ms (one tick) in 2 runs, before 9–16 ms; 256 frames: within 1.04 ms. One note length 3.6 ms off once. First version (creeping minimum, no warm-up) left the first note 16 ms late: start-up burst → warm-up added |
| 2026-11-02 | T-REC-22..28 (crosscheck #4 fixes) | Linux 88/88 ctest; MinGW build under Wine 87/87 Catch2 cases (902 assertions) | pass | Session tests with stand-ins; non-ASCII names also under Wine |
| 2026-11-02 | T-REC-01/04 (regression) | Linux VM, Midi Through + `aplaymidi`, 1024-frame buffer | pass | 21/21 events, gaps within 0.52 ms, note lengths within 1 ms |
| 2026-11-02 | `--record-test` argument check (J12) | Linux | pass | `abc`, `0`, `99999`, `5x` → message, exit code 2 |
| 2026-11-02 | T-CI-02 | GitHub Actions run 41495114618 (`c80053f`, clock smoothing) | pass | windows 5m33s (78/78), linux 2m13s (78/78), licence |
| 2026-11-02 | T-CI-02 | GitHub Actions run 41495891483 (`d6874b7`, crosscheck #4 fixes) | pass | windows 4m28s (88/88, MSVC, no compiler warnings; incl. T-REC-28 non-ASCII names on real Windows), linux 1m37s (88/88), licence |
| 2026-11-02 | T-MON-02..05 (P7) | Linux 92/92 ctest; MinGW build under Wine 91/91 Catch2 cases (995 assertions) | pass | |
| 2026-11-02 | T-MON-02 | Linux VM, GUI on Xvfb, 1 kHz −6 dBFS sine from `pw-play` linked to mini-daw's inputs with `pw-link` | pass | Track meter and text −6.0 dBFS |
| 2026-11-02 | T-MON-01 | same, Arm + Monitor clicked with xdotool | pass | Output meter −6.0 dBFS with Monitor on, −inf with it off; CLIP cleared on click. Found on the way: WirePlumber had also linked the dummy sink's monitor to mini-daw's input, a real feedback loop: input rose to +3.5 dBFS, the output limiter held 0 dBFS and both CLIP labels showed it (as designed). Arm/Monitor columns now have fixed widths (they jumped when the text changed) |
| 2026-11-02 | `--audio-test 3 --monitor` | Linux VM | pass | "Monitoring: inputs 1+2 (stereo)", output peak printed, 0 dropouts; `--monitor` alone → exit code 2 |
| 2026-11-02 | T-UI-03/04 | Windows, user, P5 build from `dist/windows` (2026-11-01) | pass | Contrast changes work |
| 2026-11-02 | T-REC-10 | Windows, user, same P5 build, real audio input | pass, with a note | 56.5 s, 24-bit stereo 48 kHz, no gaps. The user hears a soft ticking in quiet parts. Analysis: square pulses of about −45 dBFS (noise floor −62 dBFS RMS), identical on both channels, bursts of about 3 ms every 60–69 ms, at random positions relative to every buffer size (128–1024, 480, 441): not from mini-daw's data path (that would show at block boundaries), most likely electrical interference into the input. To check: record the same input with another program, and with mini-daw's GUI closed (`--record-test`) |
| 2026-11-02 | T-REC-30, T-REC-31, T-LOG-01..03 | Linux 97/97 ctest; MinGW build under Wine 96/96 Catch2 cases (1043 assertions) | pass | |
| 2026-11-02 | T-REC-32 (headless) | Linux VM, `tools/midi_test/vkeys.c` as a keyboard that comes and goes, `--record-test 12` | pass | Disconnect noticed 0.1 s after the port went, reconnected at the next 2 s check, disconnected again at its end; summary "disconnected 2 time(s)", exit code 1; all in the log |
| 2026-11-02 | T-REC-32, T-LOG-04 (GUI part) | Linux VM, Xvfb + xdotool + vkeys | pass | Track shows "DISCONNECTED - waiting for it", then the message count again without re-arming; the new port appears in the list; View → Log shows open / disconnect / connected again / list changes. Fixed on the way: MIDI port box pushed the channel box out of view (now fits; full name as tooltip) |
| 2026-11-02 | T-LOG-01 (Windows build) | Wine, GUI | pass | `mini-daw.log` written next to the `--config` file: renderer D3D11, WASAPI devices, the expected WASAPI error under Wine |
