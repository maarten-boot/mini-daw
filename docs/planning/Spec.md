# Spec — mini-daw

Status: v0.14 (2026-11-02, crosscheck #4 fixes). All earlier defaults were confirmed by the user on 2026-11-01.

## 1. Scope
A small desktop program that records MIDI and audio input on several tracks at once, with live monitoring and playback of takes. A later phase adds VST3 instruments: their audio output can be recorded live, and recorded MIDI takes can be rendered through them to audio.

- **In scope:** recording, monitoring, playback, VST3 instrument hosting (phase 2), MIDI-to-audio rendering through VST3 (phase 2).
- **Not in v1:** note (MIDI) editing and sound (audio) editing. The design must leave room to add them later.
- **Platforms:** Windows 10 first; Linux and macOS later. Platform-specific code stays behind an abstraction layer.

## 2. Technology
- **SPEC-TECH-01** Language: C++ (C++20), build system CMake.
- **SPEC-TECH-02** Audio I/O: RtAudio. Windows backend WASAPI first. ASIO as an option later; see SPEC-LIC-03.
- **SPEC-TECH-03** MIDI I/O: RtMidi.
- **SPEC-TECH-04** Plugin hosting: Steinberg VST3 SDK, used directly.
- **SPEC-TECH-05** GUI: Dear ImGui on GLFW. Rendering: **Direct3D 11 on Windows**, falling back to WARP (Windows' built-in software renderer) when there is no usable GPU driver, as in VMs without 3D acceleration; OpenGL 3.0 on Linux/macOS. The status line shows which renderer is in use; `MINIDAW_D3D_WARP=1` forces WARP for testing. If no renderer can start, the program shows a message box instead of failing silently. GLFW provides the native window handle that VST3 plugin editors need. On Linux GLFW is built for X11 only for now (works under XWayland); native Wayland comes with P12.
- **SPEC-TECH-06** Only libraries whose licence is compatible with MIT (MIT, BSD, zlib, Boost or similar). See section 6.

## 3. Functional requirements

### 3.1 Devices
- **SPEC-DEV-01** List the available audio input/output devices and MIDI input and output ports.
- **SPEC-DEV-02** The user selects the audio device, sample rate and buffer size. Default 48 kHz.
- **SPEC-DEV-03** Device settings are saved and restored on the next start.
- **SPEC-DEV-02a** Implementation: Settings → Audio devices… chooses input device, output device (each also `(default)` or `(none)`), sample rate (44.1/48/88.2/96 kHz) and buffer size (64–2048 frames). Apply reopens the stream; on failure the previous settings are reopened and the error is shown. All input channels of the input device are opened (up to 32) and stereo output. The window shows the actual rate, buffer size, reported latency, dropouts and a peak level per input channel as text (dBFS).
- **SPEC-DEV-03a** Devices are stored **by name** in `[audio]` of `config.ini` (RtAudio ids are not stable between runs). A saved device that is missing falls back to the system default with a visible note; the saved choice is kept, so it is used again when the device returns.
- **SPEC-DEV-05** A stream only counts as running after the first audio callback has arrived (within 1 s). Otherwise the driver's message is shown as an error. If the audio thread could not get realtime priority (Linux), a warning says so.
- **SPEC-DEV-06** `mini-daw --audio-test <seconds>` opens the configured devices without the GUI and prints the actual settings, peak levels, callback count and dropouts.

### 3.2 Tracks and recording
- **SPEC-REC-01** The project has several tracks. Each track is an audio track (fed by one or two audio input channels) or a MIDI track (fed by one MIDI input port and channel, or all channels).
- **SPEC-REC-02** Tracks can be armed for recording. All armed tracks record together, sample-synchronised, when Record is pressed. All takes start in the same audio block and keep the same timeline: audio lost in one take (SPEC-REC-05b) is replaced by silence of the same length, so later audio stays in place and all takes of a recording have the same length.
- **SPEC-REC-03** Audio takes are saved as WAV files, 24-bit PCM at the session sample rate.
- **SPEC-REC-04** MIDI takes are saved as Standard MIDI Files (type 1), with timestamps aligned to the audio timeline.
- **SPEC-REC-05** No data is lost if a recording runs at least 60 minutes. Disk writing happens outside the audio thread.
  - **SPEC-REC-05a** While a take is being recorded, its WAV header is refreshed about once a second, so after a crash or power loss the file is valid and holds the audio up to that point. For power loss the file is also forced to the disk at each refresh (`FlushFileBuffers` / `fsync`). This can be switched off for slow disks with `sync_to_disk = false` in `[recording]` of `config.ini` (default `true`); then only a program crash is covered. A take is always forced to disk when it is closed.
  - **SPEC-REC-05b** If audio is lost anyway (the disk cannot keep up, the disk is full), the program says so: the number of lost frames per take and the disk error are reported, never hidden. Lost frames include audio that could not be buffered and audio that could not be written after a disk error. Lost MIDI messages are shown next to the lost frames while recording. The audio thread buffers at least 4 seconds per take (the buffer is rounded up to a power of two: about 5.5 s at 48 kHz). After a disk error the take stops; its file is cut back to the last complete write and keeps a valid header.
  - **SPEC-REC-05c** A plain WAV file can hold at most 4 GiB (about 8 hours of 24-bit mono or 4 hours of stereo at 48 kHz). A take that reaches this limit stops with a clear message; RF64 for longer takes is a later option.
- **SPEC-REC-01a** Implementation (P6): a track list in the main window. *Add audio track* (input channel, or two channels for stereo) and *Add MIDI track* (MIDI input port and channel 1–16 or All); name editable; remove. Until projects exist (P9) the track list is kept in memory only; when the audio stream first runs and the user has not made a track yet, one audio track on inputs 1+2 is added (mono if the device has one input). Tracks cannot be changed while recording. Each track shows its own input level (audio, dBFS) or message count (MIDI); the list of all input levels is folded away under "Input levels". MIDI input ports are opened only for armed MIDI tracks (Windows lets only one program open a port); a port that does not open (unplugged, in use) is tried again about every 2 seconds and right before Record.
- **SPEC-REC-02a** Each track has an Arm checkbox; the armed state is also shown as text ("armed"), not only by colour (SPEC-UI-07). *Record* (button, or Ctrl+R) starts all armed tracks; *Stop* (button, or Ctrl+R again) ends them. While recording the window shows elapsed time, lost frames and any disk error. Recording needs a running audio stream (it provides the timeline), also for MIDI-only recordings.
- **SPEC-REC-04a** MIDI timestamps: the MIDI input thread stamps each message with a steady clock; the audio callback publishes, once per block, the frame position at the end of the block it just received with the clock time of the callback; a message's position on the audio timeline is the latest pair's frame plus the elapsed clock time × sample rate. The callback time jitters by up to a buffer (the audio thread can be woken late, never early), so it is smoothed first: for an ideal stream "clock time − frame / rate" is constant, and the smallest value of the last 0.5–1 s is used (this also follows drift between the audio and system clocks); the first 0.5 s after a stream start use the raw time (drivers often deliver the first blocks in a burst), and a dropout or a jump of more than 50 ms (or 3 buffers) restarts the smoothing. The driver's input latency is not added yet; T-REC-04 measures the offset. Expected accuracy: about 1 ms between MIDI messages, independent of the buffer size. Time 0 of a MIDI take is the first frame of the audio takes recorded with it; MIDI that arrives before that frame (up to one buffer after pressing Record) is not recorded, so a take can start with a note-off. The end of a MIDI take is the end of the recording's timeline (all frames recorded), also when one audio take stopped early. A MIDI message that cannot be placed on the timeline is counted as lost.
- **SPEC-REC-04b** MIDI file: Standard MIDI File type 1: track 0 holds the tempo and the take name, then one track per MIDI track. Timing is chosen by `midi_timing` in `[recording]`: `ppq` (default; 960 ticks per quarter note at a fixed 120 BPM, 0.52 ms per tick) or `smpte` (25 fps × 40 = 1 ms per tick, no tempo). Recorded: all channel messages (notes, controllers, pitch bend, program change, aftertouch). Not recorded in v1: SysEx and MIDI clock/real-time messages. A track set to one channel records only that channel. Notes still held at Stop get a note-off at the end of the take.
- **SPEC-REC-04c** The MIDI file of a running take is rewritten about once a second (via a temporary file and a rename, with the same `sync_to_disk` rule as audio), so a crash loses at most about the last second of MIDI too. A failed rewrite is retried the next second and only stays an error if the final write fails; if the audio stream stops during a recording, the MIDI files of the last rewrite are kept.
- **SPEC-REC-07** Recording folder: `folder` in `[recording]` (empty = default: Windows `Documents\mini-daw`, Linux and macOS `~/Music/mini-daw`), changeable in Settings → Recording…, which also has `midi_timing` and `sync_to_disk`. Folder, track and file names may contain any characters the file system allows (stored as UTF-8). In `config.ini`, a folder written without quotes is used as typed (`folder = \\server\takes`); inside quotes `\\` stands for one backslash and `\"` for a quote, except that a final `\"` closes the value (`"D:\Takes\"`). Each Record press creates a subfolder `take-YYYYMMDD-HHMMSS` with one file per armed track: `NN-<track name>.wav` or `.mid` (NN = track number; characters not allowed in file names are replaced by `_`).
- **SPEC-REC-06** `mini-daw --record-test <seconds> <folder>` (1–3600 s) records input channel 1 (and 2, if the device has it) into a take subfolder of the folder (SPEC-REC-07 naming), without the GUI, and prints frames written, lost frames, clipped samples and dropouts (xruns). From P6 it also records every open MIDI input port (all channels) to a MIDI file in the same take folder, and prints the number of MIDI events per port. Exit code 0 when nothing was lost, 1 when audio or MIDI was lost or an error occurred, 2 for a bad command line (e.g. seconds not a whole number from 1 to 3600).

### 3.3 Monitoring
- **SPEC-MON-01** Input monitoring can be switched on or off per armed audio track.
- **SPEC-MON-02** Each track shows an input level meter.

### 3.4 Playback / transport
- **SPEC-PLAY-01** Transport controls: Play, Stop, Record, return to start.
- **SPEC-PLAY-02** Recorded audio takes play back in sync on the output device.
- **SPEC-PLAY-03** Recorded MIDI takes play to a MIDI output port, or to the track's VST3 instrument (phase 2).
- **SPEC-PLAY-04** Each track has a volume control and mute/solo buttons.

### 3.5 VST3 instruments (phase 2)
- **SPEC-VST-01** Scan the standard VST3 folders (Windows: `C:\Program Files\Common Files\VST3`) and list the instruments found.
- **SPEC-VST-02** An instrument can be loaded on a MIDI track. Live MIDI input drives it, and its audio output is monitored.
- **SPEC-VST-03** The instrument's audio output can be recorded live to a WAV take.
- **SPEC-VST-04** A recorded MIDI take can be rendered through the instrument to a WAV file, offline or in real time.
- **SPEC-VST-05** The plugin's own editor window can be opened.
- **SPEC-VST-06** The plugin's state (preset) is saved with the project.

### 3.6 Project
- **SPEC-PRJ-01** A project is a folder containing a project file (JSON) and the take files.
- **SPEC-PRJ-02** Projects can be saved and reopened.

## 4. UI accessibility requirements (GUI)
- **SPEC-UI-01** Default theme is a white background with black text.
- **SPEC-UI-02** Every foreground/background colour pair meets a WCAG 2.x contrast ratio of at least 4.5:1 (AA).
- **SPEC-UI-03** The user can raise the minimum contrast interactively while the application runs.
- **SPEC-UI-04** The user can set the minimum contrast in a config file.
- **SPEC-UI-05** Contrast presets: AA (4.5:1), AAA (7:1), Max (21:1, pure black/white), plus a custom minimum ratio. A custom value below 4.5:1 is rejected.
- **SPEC-UI-06** When a minimum is active, any colour pair below it is adjusted until it meets that minimum.
- **SPEC-UI-03a** Implementation of UI-03: menu View → Contrast (AA, AAA, Max, Custom…, Raise, Lower) and the shortcuts Ctrl+Shift+Up / Ctrl+Shift+Down, which step through AA → AAA → Max. An interactive change is saved to the config file immediately.
- **SPEC-UI-04a** Implementation of UI-04: INI file, key `min_contrast` in section `[ui]`, value `AA`, `AAA`, `Max` or a number from 4.5 to 21. Default location: Windows `%APPDATA%\mini-daw\config.ini`, Linux `~/.config/mini-daw/config.ini`, macOS `~/Library/Application Support/mini-daw/config.ini`; `--config <file>` overrides it. Saving keeps the user's own comments and other lines. An invalid value gives a visible warning and falls back to AA.
- **SPEC-UI-02a** Contrast is checked on a fixed list of foreground/background pairs between colour roles (`colorPairs()`). Every ImGui colour slot maps to a role, so no toolkit default escapes the check. Borders, separators, check marks and scrollbar thumbs are held to the same minimum as text (stricter than WCAG's 3:1 for non-text). `View → Contrast report` and `mini-daw --contrast-report` list every pair with its ratio.
- **SPEC-UI-08** Text size is adjustable from 12 to 32 px (default 16 px): View → Text size, Ctrl+= / Ctrl+- / Ctrl+0, and `font_size` in `[ui]` of `config.ini`. Uses ImGui's built-in scalable font.
- **SPEC-UI-07** Level meters and other status shown by colour also show it another way, such as a text value or pattern.

## 5. Non-functional requirements
- **SPEC-NF-01** The audio thread never blocks: no locks, memory allocation or file I/O in the audio callback.
- **SPEC-NF-02** Round-trip latency with WASAPI exclusive mode at a 256-sample buffer stays at or below about 20 ms.
- **SPEC-NF-03** A plugin that crashes or misbehaves must not corrupt takes already written to disk.

## 6. Licensing
- **SPEC-LIC-01** The project is released under the MIT licence (copyright Maarten Boot): free for anyone to use and modify.
- **SPEC-LIC-02** Every bundled or fetched dependency has an MIT-compatible licence, and `THIRD_PARTY.md` lists each one with its licence. Checked so far: RtAudio and RtMidi (MIT-style), Dear ImGui (MIT), GLFW (zlib), VST3 SDK (MIT, its LICENSE file on GitHub checked 2026-11-01), Catch2 (Boost).
- **SPEC-LIC-03** ASIO support is an optional build setting `MINIDAW_WITH_ASIO`, off by default. The ASIO SDK is never put in the repository; whoever builds with ASIO downloads it. Steinberg offers it under GPLv3 or a proprietary licence (https://www.steinberg.net/developers/asiosdk-open/). A program built with ASIO under GPLv3 is itself GPLv3, and the README says so.
- **SPEC-LIC-04** The ASIO name and logo are Steinberg trademarks and are not covered by the GPLv3. If we use them:
  - the logo is only shown unaltered, and only in product-related places (splash screen, About box, website, documentation);
  - "ASIO" is never part of the product name or any build name (for example, not "mini-daw-ASIO");
  - the logo is only shown in builds that actually support ASIO.
  
  By default no logo is shown; the driver list may say "ASIO" as plain text.

## 7. Build and CI
- **SPEC-CI-01** Every GitHub Actions step (or other CI action) is pinned to a full commit SHA, with the release tag as a comment, using the latest release that works. Any exception is written down here with the reason. Exceptions so far: none.
- **SPEC-CI-02** CI builds and tests on Windows (primary target) and Linux, and runs the licence and pinning check. The Windows and Linux jobs smoke-run the program and publish it as a download (`mini-daw-windows-x64`, `mini-daw-linux-x64`). Runner images are pinned to a named version (`windows-2025`, `ubuntu-24.04`), not `-latest`.
- **SPEC-CI-03** The project can be cross-compiled for Windows from Linux with MinGW-w64 (`cmake/toolchains/mingw-w64.cmake`).

## 8. Known limitations
- **Max (21:1):** every background becomes white, so hovered, pressed and selected states can no longer be told apart by colour; only borders and the focus cursor remain. To be solved together with SPEC-UI-07 (state shown another way than colour).
- **Windows 10 N editions** have no `MFPlat.dll` (used by RtAudio's WASAPI code) unless the Media Feature Pack is installed. The program then does not start.
- **WASAPI exclusive mode:** RtAudio 6.0.1 opens WASAPI in shared mode only. Shared mode converts sample rates automatically (Line In 44.1 kHz + Speakers 48 kHz works), but SPEC-NF-02 (≈20 ms round trip in exclusive mode) cannot be reached this way. Options: measure shared-mode latency first (T-NF-02); then either extend RtAudio, write an own WASAPI exclusive backend, or rely on ASIO (P11b).
- **WASAPI under Wine:** RtAudio's WASAPI client fails to start under Wine 10 in this VM ("Unable to initialize capture/render audio client"); the error is now reported (SPEC-DEV-05). Only real Windows tests count for audio.

## 8b. Observations from the first Windows runs (2026-11-01)
- The first GUI start failed on the user's Windows 10 machine: `GLFW error 65542: WGL: The driver does not appear to support OpenGL` (no OpenGL driver, probably a VM). Fixed by switching Windows to Direct3D 11 with WARP fallback (SPEC-TECH-05).
- Input and output devices can have different native sample rates (Line In 44.1 kHz, Speakers 48 kHz on the user's machine). Handled since P4: one session sample rate (SPEC-DEV-02, default 48 kHz); WASAPI shared mode converts, and a warning is shown when the stream runs at a different rate than requested. Still to confirm on the real machine with an active input (T-DEV-07).

## 9. Open questions
- When should ASIO support come? Its licence is settled in SPEC-LIC-03.
- Should the whole project be exportable as one stereo mix? (Not answered yet.)
- Should the WASAPI build avoid `MFPlat.dll` so it also starts on Windows 10 N without the Media Feature Pack?
- Metronome/click track: not for now (user, 2026-11-01).

Settled: GitHub repository `maarten-boot/mini-daw` (2026-11-01); the user pushes, Claude checks CI.
