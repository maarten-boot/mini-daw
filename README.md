# mini-daw

a simple daw just for recording and playback with VST3 instrument support

A small program to record MIDI and audio input on several tracks at once, with monitoring and playback. VST3 instrument hosting comes later. Windows 10 first; Linux and macOS later.

**Status:** early version (steps P2–P7). The program lists audio devices and MIDI ports, lets you choose the audio devices, sample rate and buffer size, and records audio and MIDI tracks: add tracks, arm them and press Record. Each track has a level meter (with dBFS value and a CLIP label) and, for audio tracks, a Monitor switch to hear the input while armed (use headphones). MIDI keyboards can be unplugged and plugged in again; they reconnect by themselves. View → Log (Ctrl+L) shows a diagnostics log, also kept as `mini-daw.log` next to `config.ini` (on Windows in `%APPDATA%\mini-daw`). Playback and saving projects come next. Adjustable contrast and text size. See `docs/planning/` for the Spec, Plan and test cases.

## Building

You need CMake 3.24 or newer, a C++20 compiler and Git. Dependencies are downloaded automatically at configure time (see `THIRD_PARTY.md`).

### Windows (Visual Studio 2022 or newer)

```
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\src\Release\mini-daw.exe
```

### Linux

Install the development packages first, for example on Debian/Ubuntu:

```
sudo apt install build-essential cmake ninja-build libasound2-dev libpulse-dev xorg-dev libgl-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/mini-daw
```

### Cross-compiling for Windows from Linux (MinGW-w64)

```
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIDAW_BUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake
cmake --build build-win
```

### Command line

```
mini-daw                     open the GUI
mini-daw --config <file>     use this config file instead of the default one
mini-daw --list-devices      print audio devices and MIDI ports, then exit
mini-daw --audio-test <sec>  open the configured audio devices, print settings and levels
                             (add --monitor to hear inputs 1+2 and print the output level)
mini-daw --record-test <sec> <folder>
                             record input 1 (and 2) to a 24-bit WAV file in <folder>
mini-daw --contrast-report   print every colour pair and its contrast, then exit
mini-daw --version           print the version, then exit
```

## Screen contrast

The default theme is black text on a white background, and every colour combination on screen has a contrast ratio of at least 4.5:1 (WCAG AA). You can raise it:

- **In the program:** View → Contrast (AA 4.5:1, AAA 7:1, Max 21:1, or a custom value), or Ctrl+Shift+Up / Ctrl+Shift+Down. The choice is saved.
- **In the config file:**

  ```
  [ui]
  min_contrast = AAA   ; AA, AAA, Max, or a number from 4.5 to 21
  ```

  The file is `%APPDATA%\mini-daw\config.ini` on Windows and `~/.config/mini-daw/config.ini` on Linux.

View → Contrast report shows every colour pair and its ratio.

## Text size

View → Text size, or Ctrl+= / Ctrl+- / Ctrl+0, or `font_size = 20` under `[ui]` in the config file (12 to 32 px, default 16).

## Audio devices

Settings → Audio devices… selects the input and output device, sample rate and buffer size. The choice is saved under `[audio]` in the config file, by device name. If a saved device is missing, the program uses the system default and tells you so. To check the audio without the GUI:

```
mini-daw --audio-test 5
```

## Recording

The main window has a track list. *Add audio track* (one input, or two for stereo) and *Add MIDI track* (a MIDI input port, all channels or one). Tick *Arm* on the tracks to record, then press *Record* (or Ctrl+R); press it again to stop. Each recording goes into its own folder `take-YYYYMMDD-HHMMSS` inside the recording folder (Settings → Recording…; default `Documents\mini-daw` on Windows, `~/Music/mini-daw` on Linux), with one file per track: `01-Audio 1+2.wav`, `02-Keys.mid`, …

Audio is 24-bit WAV. MIDI is a Standard MIDI File; by default with 960 ticks per quarter note at a fixed 120 BPM (0.52 ms resolution, opens cleanly in any DAW). For free playing without a tempo you can switch to SMPTE timing (1 ms ticks) in Settings → Recording… or with `midi_timing = smpte` under `[recording]`. Both keep your timing exactly; PPQ only adds a bar grid that does not mean anything for unmetered playing. The track list is not saved yet (projects come later).

In `config.ini`, write a network folder without quotes (`folder = \\server\takes`); inside quotes, `\\` stands for one backslash.

To try recording without the GUI:

```
mini-daw --record-test 10 C:\Users\me\Music\mini-daw
```

This records input 1 (and 2) and every MIDI input into a take folder in the given folder and reports lost frames, clipped samples and dropouts; the exit code is 1 if audio was lost. While a take is being recorded its file is kept valid and forced to the disk about once a second, so a crash or power loss loses at most the last second. If audio is lost (disk too slow), the gap is filled with silence so the rest of the take stays in time, and the loss is reported.

On a slow disk the once-a-second sync can be switched off (then only a program crash is covered):

```
[recording]
sync_to_disk = false
```

## Graphics

On Windows the GUI uses Direct3D 11. On machines without a usable graphics driver (for example a virtual machine without 3D acceleration) it falls back to WARP, Windows' built-in software renderer. The status line shows which one is in use. Linux uses OpenGL 3.0.

## Licence

mini-daw is released under the MIT licence (see `LICENSE`): anyone may use and modify it.

### About ASIO

ASIO support is **optional and off by default**, and is planned for a later step. The Steinberg ASIO SDK is never included in this repository. Steinberg offers it under GPLv3 or under its own proprietary licence (https://www.steinberg.net/developers/asiosdk-open/).

- The mini-daw source code stays MIT in all cases.
- If you build with `-DMINIDAW_WITH_ASIO=ON` and use the ASIO SDK under GPLv3, the resulting **binary is GPLv3** and must be distributed under GPLv3 terms, including its source code.
- ASIO is a trademark of Steinberg Media Technologies GmbH. It is not part of the mini-daw name.
