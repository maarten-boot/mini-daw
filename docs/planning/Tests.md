# Test cases — mini-daw

Type: **A** = automated unit/integration test, **M** = manual test.
Status column in the Results section at the end.

## UI / contrast
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-UI-01 | SPEC-UI-01 | M | Start the app with no config file | White background, black text |
| T-UI-02 | SPEC-UI-02 | A | Compute the contrast ratio of every colour pair in the default theme | All ≥ 4.5:1 |
| T-UI-03 | SPEC-UI-03 | M | Switch the preset AA → AAA → Max at runtime | Colours update right away; all pairs ≥ 7:1, then 21:1 |
| T-UI-04 | SPEC-UI-04 | A | Set `min_contrast = 7.0` in the config file, then start the app | All pairs ≥ 7:1 |
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
| T-DEV-04 | SPEC-DEV-03 | M | Restart after the saved device has been unplugged | Clear message; falls back to the default device; no crash |

## Recording
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-REC-01 | SPEC-REC-01/02 | M | Arm 2 audio tracks and 1 MIDI track, record 30 s | 2 WAV files and 1 MIDI file, all the same length |
| T-REC-02 | SPEC-REC-03 | A | WAV writer: write a known sine wave, read it back | Header valid (24-bit, correct rate); samples match |
| T-REC-03 | SPEC-REC-04 | A | SMF writer: write known events, parse them back | Type 1 file; events and timing match |
| T-REC-04 | SPEC-REC-04 | M | Loop audio out to audio in, send MIDI notes that also trigger a click, record both | MIDI note-on and audio click within 1 buffer of each other |
| T-REC-05 | SPEC-REC-05, NF-01 | M | Record 4 tracks for 60 minutes | No dropouts or xruns logged; files complete |
| T-REC-06 | SPEC-REC-05 | A | Ring buffer stress test: producer and consumer threads, 10⁸ samples | No lost or duplicated samples |

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

## Project
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-PRJ-01 | SPEC-PRJ-01/02 | A | Save a project with 3 tracks, reopen it | Tracks, takes and settings identical |
| T-PRJ-02 | SPEC-PRJ-02 | A | Open a project with a missing take file | Clear message; the rest loads |

## Licensing
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-LIC-01 | SPEC-LIC-01 | A | `LICENSE` file exists at the repository root | MIT text with "Copyright (c) 2026 Maarten Boot" |
| T-LIC-02 | SPEC-LIC-02 | A | CI script checks that every dependency in the build has an entry in `THIRD_PARTY.md` with an allowed licence (MIT, BSD, zlib, Boost, Apache-2.0) | Passes; fails if an entry is missing or the licence is not allowed |
| T-LIC-03 | SPEC-LIC-03 | A | Default configure and build (no options set) | Builds; no ASIO symbols or headers involved; repository contains no ASIO SDK files |
| T-LIC-04 | SPEC-LIC-03 | M | Configure with `-DMINIDAW_WITH_ASIO=ON` and no SDK path set | Configure stops with a clear message explaining where to get the SDK and what its licence means |
| T-LIC-05 | SPEC-LIC-04 | M | Look at the product name, build names, About box and installer, with and without ASIO enabled | No "ASIO" in any name; logo, if any, only in ASIO builds and only in the About box, splash screen or docs |

## Build and CI
| ID | Spec | Type | Description | Expected result |
|----|------|------|-------------|-----------------|
| T-CI-01 | SPEC-CI-01 | A | `tools/check_licenses.py` scans `.github/workflows/*.yml` | Every `uses:` is pinned to a 40-character SHA; fails otherwise |
| T-CI-02 | SPEC-CI-02 | A | Push to GitHub | Windows, Linux and licence jobs all pass |
| T-CI-03 | SPEC-CI-03 | M | Cross-compile with the MinGW toolchain file | `mini-daw.exe` and `minidaw_tests.exe` build without errors |
| T-DEV-05 | SPEC-DEV-01 | A | `scanDevices()` on a machine without audio/MIDI hardware | Does not throw; problems are returned as warnings |

## Results
| Date | Test | Environment | Result | Notes |
|------|------|-------------|--------|-------|
| 2026-11-01 | T-LIC-02 | Linux | pass | Also checked against planted failures: wrong version, GPL licence, unpinned action, `asio.h` file. All 4 caught. |
| 2026-11-01 | T-LIC-03 | Linux | pass | No ASIO SDK files; default build has no ASIO |
| 2026-11-01 | T-LIC-04 | Linux + MinGW | pass | Clear message on Linux ("Windows only") and on Windows without an SDK path |
| 2026-11-01 | T-LIC-05 | Linux | pass (partial) | Product name checked by unit test; About box, installer and logo come later |
| 2026-11-01 | T-CI-01 | Linux | pass | |
| 2026-11-01 | T-CI-02 | — | not run | No GitHub remote yet |
| 2026-11-01 | T-CI-03 | Linux (MinGW 13) | pass | Not run under Wine: root disk 98% full |
| 2026-11-01 | T-DEV-05 | Linux VM (no sound card) | pass | ALSA sequencer access denied → returned as MIDI warnings |
