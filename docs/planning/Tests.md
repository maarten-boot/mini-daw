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

## Devices
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-DEV-01 | SPEC-DEV-01 | M | Open device settings with a USB audio interface and a MIDI keyboard connected | Both appear in the lists |
| T-DEV-02 | SPEC-DEV-02 | M | Select 44.1 kHz and a 128-sample buffer | Stream runs at those settings |
| T-DEV-03 | SPEC-DEV-03 | M | Restart the app | The previous device and settings are restored |
| T-DEV-04 | SPEC-DEV-03 | A+M | Restart after the saved device has been unplugged | Clear message; falls back to the default device; no crash; saved choice kept |
| T-DEV-06 | SPEC-DEV-05 | M | Start with a driver that fails on its own thread (e.g. WASAPI under Wine) | Error with the driver's message; not shown as running |
| T-DEV-07 | SPEC-DEV-06 | M | `mini-daw --audio-test 5` on Windows 10 with Line In + Speakers at 48 kHz | Runs; callbacks ≈ expected; 0 dropouts; peak levels change when a signal is applied |

## Recording
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-REC-01 | SPEC-REC-01/02 | M | Arm 2 audio tracks and 1 MIDI track, record 30 s | 2 WAV files and 1 MIDI file, all the same length |
| T-REC-02 | SPEC-REC-03 | A | WAV writer: write a known sine wave, read it back | Header valid (24-bit, correct rate); samples match |
| T-REC-03 | SPEC-REC-04 | A | SMF writer: write known events, parse them back | Type 1 file; events and timing match |
| T-REC-04 | SPEC-REC-04 | M | Loop audio out to audio in, send MIDI notes that also trigger a click, record both | MIDI note-on and audio click within 1 buffer of each other |
| T-REC-05 | SPEC-REC-05, NF-01 | M | Record 4 tracks for 60 minutes | No dropouts or xruns logged; files complete |
| T-REC-06 | SPEC-REC-05 | A | Ring buffer stress test: producer and consumer threads, 10⁸ samples | No lost or duplicated samples |
| T-REC-07 | SPEC-REC-05a | A | Write a WAV file, refresh the header, write more, read the file before closing | File is valid and holds the audio up to the header refresh; after closing, all of it |
| T-REC-08 | SPEC-REC-03, 05, NF-01 | A | Recorder with 4 input channels and 2 takes (mono ch 3; stereo ch 4+1), a test thread plays the audio callback for 5 s, the real disk thread writes | Both files hold every frame, sample-exact, in the right channel order; 0 lost frames |
| T-REC-09 | SPEC-REC-05b | A | Recorder without a disk thread and a 4096-frame ring; feed 10 000 frames | 6000 frames reported lost; the 4000 buffered frames are still written on stop |
| T-REC-10 | SPEC-REC-06 | M | `mini-daw --record-test 10 <folder>` on Windows 10 with a working input, while speaking or playing into it | A 10 s, 24-bit WAV in the folder that plays back what was heard; 0 lost frames, 0 xruns |

## Monitoring
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-MON-01 | SPEC-MON-01 | M | Monitoring on, then off, on an armed track | Input is heard, then silent |
| T-MON-02 | SPEC-MON-02 | M | Feed a −6 dBFS sine wave | Meter reads −6 dB ± 0.5 |

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
| T-DEV-05 | SPEC-DEV-01 | A | `scanDevices()` on a machine without audio/MIDI hardware | Does not throw; problems are returned as warnings |

| T-UI-10 | SPEC-TECH-05 | M | Start the GUI on Windows without a GPU/OpenGL driver (VM), and with `MINIDAW_D3D_WARP=1` | Window opens; status line says "Direct3D 11 (WARP software renderer)" |
| T-UI-11 | SPEC-TECH-05 | M | Make every renderer fail (e.g. Windows Basic Display with D3D disabled) | A message box explains the problem; no silent exit |

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
