# Spec — mini-daw

Status: v0.9 (2026-11-01, P5 implemented). All earlier defaults were confirmed by the user on 2026-11-01.

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
- **SPEC-REC-02** Tracks can be armed for recording. All armed tracks record together, sample-synchronised, when Record is pressed.
- **SPEC-REC-03** Audio takes are saved as WAV files, 24-bit PCM at the session sample rate.
- **SPEC-REC-04** MIDI takes are saved as Standard MIDI Files (type 1), with timestamps aligned to the audio timeline.
- **SPEC-REC-05** No data is lost if a recording runs at least 60 minutes. Disk writing happens outside the audio thread.
  - **SPEC-REC-05a** While a take is being recorded, its WAV header is refreshed about once a second, so after a crash or power loss the file is valid and holds the audio up to that point.
  - **SPEC-REC-05b** If audio is lost anyway (the disk cannot keep up, the disk is full), the program says so: the number of lost frames per take and the disk error are reported, never hidden. The audio thread buffers about 4 seconds per take.
  - **SPEC-REC-05c** A plain WAV file can hold at most 4 GiB (about 8 hours of 24-bit mono or 4 hours of stereo at 48 kHz). A take that reaches this limit stops with a clear message; RF64 for longer takes is a later option.
- **SPEC-REC-06** `mini-daw --record-test <seconds> <folder>` records input channel 1 (and 2, if the device has it) to `take-YYYYMMDD-HHMMSS.wav` in the folder, without the GUI, and prints frames written, lost frames, clipped samples and dropouts.

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
- Input and output devices can have different native sample rates (Line In 44.1 kHz, Speakers 48 kHz on the user's machine). P4/P5 must handle this: one session sample rate (SPEC-DEV-02, default 48 kHz), with WASAPI shared-mode conversion or a clear message when a device cannot run at that rate.

## 9. Open questions
- When should ASIO support come? Its licence is settled in SPEC-LIC-03.
- Should the whole project be exportable as one stereo mix? (Not answered yet.)
- GitHub: no remote until the user has reviewed a first draft (user, 2026-11-01). Account/organisation still to decide.
- Should the WASAPI build avoid `MFPlat.dll` so it also starts on Windows 10 N without the Media Feature Pack?
- Metronome/click track: not for now (user, 2026-11-01).
