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
| T-REC-18 | SPEC-REC-04a | A | Clock → frame mapping with a known (frame, time) pair at 48 kHz | Event 10 ms after the pair maps to +480 frames; seqlock reader never sees a half-written pair (stress with two threads) |
| T-REC-19 | SPEC-REC-07, 01a | A | Take folder and file names: track names with `/ \ : * ? " < > |`, duplicate folder name in the same second; default tracks for 1 and 2 inputs | Safe names; second folder gets `-2`; one mono or one stereo track |
| T-REC-20 | SPEC-REC-04c | A | Rewrite a MIDI take while recording, then read it | Valid file with the events so far; no `.tmp` left after stop |
| T-REC-21 | SPEC-REC-02a | M | GUI: arm 1 audio + 1 MIDI track, Ctrl+R, play, Ctrl+R | Take folder with one WAV and one MID of the same length; elapsed time shown while recording; "armed" visible as text |
| T-REC-22 | SPEC-REC-07 | A+M | `[recording] folder`, `midi_timing`, `sync_to_disk` in the config; Settings → Recording… | Parsed with warnings for bad values; changes saved |
| 2026-11-02 | T-REC-16..20, T-REC-22 | Linux 76/76 ctest; MinGW build under Wine 75/75 Catch2 cases (783 assertions) | pass | SMF ppq/smpte round trip, take builder, clock mapping + seqlock stress (2·10⁶ writes, 0 torn reads), take names, config incl. Windows paths with backslashes |
| 2026-11-02 | T-REC-21 (audio part) | Linux VM, Xvfb + xdotool | pass (partial) | Arm, Ctrl+R, 2.5 s, Ctrl+R: take folder with `01-Audio 1+2.wav` (128 000 frames), elapsed time shown, "armed" as text, widgets disabled while recording. MIDI part needs sequencer access (`audio` group) |
| 2026-11-02 | T-REC-10 | Linux VM (PulseAudio) | pass (Linux only) | `--record-test 2` through the session: take folder, 97 280 frames, 0 lost, 0 dropouts; MIDI scan warning printed (no sequencer access) |
