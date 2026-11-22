# mini-daw

a simple daw just for recording and playback with VST®3 instrument support

A small program to record MIDI and audio input on several tracks at once, with monitoring and playback, and VST3 instruments on MIDI tracks. For Windows 10 / 11 and Linux; macOS as a preview (built and tested automatically, not yet tried on a real Mac).

**Status:** version 1.1.0 (2026-11-22; steps P2–P34, PL1, PL2: 1.0.0, the first release, for Windows and Linux; 1.1.0 adds macOS as a preview; what is in it: `CHANGELOG.md`; the version is shown at the right of the menu bar and in Help → About, with the commit and CI run of the build). The program lists audio devices and MIDI ports, lets you choose the audio devices, sample rate and buffer size, and records audio and MIDI tracks: add tracks, arm them and press Record. Each track has a level meter (with dBFS value and a CLIP label) and, for audio tracks, a Monitor switch to hear the input while armed (use headphones). MIDI keyboards can be unplugged and plugged in again; they reconnect by themselves. View → Log (Ctrl+L) shows a diagnostics log, also kept as `mini-daw.log` next to `config.ini` (on Windows in `%APPDATA%\mini-daw`). Play back what you recorded (Space = Play/Stop, Home = to the start), with volume, mute and solo per track; MIDI tracks play to a MIDI output you choose. Record while other tracks play to overdub along with them; File → Open take folder… loads earlier takes. Projects: File → New project… keeps everything (tracks, mix, takes) in one folder, saved automatically and reopened at the next start; Save project as… copies it with its takes. Windows: an installer (`mini-daw-<version>-setup.exe`, from the CI downloads). VST3 instruments: Settings → Instruments (VST3)… scans the plugin folders (add your own with **Browse…**, remove them with **Remove**; every folder field has a Browse… button); choose an instrument in a MIDI track's output list; it plays live when the track is armed and plays the track's take, and is saved with its settings in the project. **Presets** beside it lists the instrument's own programs and its `.vstpreset` files (with **<** / **>** to step through them), and **Save preset…** keeps the current sound as a preset of your own (in `Documents\VST3 Presets`, so other programs see it too). The track's **Edit** button opens the instrument's own window (sounds, presets), on Windows and on Linux (X11); it can stay open while you play. Recording an armed instrument track also records the instrument's sound, onto a linked audio track below it ("<name> sound"); **Render** makes that sound from the MIDI take, faster than real time. The linked track's **Sound from** chooses what plays: the instrument (default) or the recording. **Auto record** records everything you play on the armed tracks without pressing Record: a part starts when you play and ends after a pause (5 s, adjustable in Settings → Recording…), each part in its own folder `auto/<date time>`. The transport (Play, Record, Auto record …) is above the tabs, the same on every tab. The audio stream and the device list are on the **Audio setup** tab (the Recording tab keeps a short status line); drag a tab, or use Settings → Tabs…, to change their order (Terms stays at the right end), and the tab used last opens at the next start. The **Timeline** tab shows all recordings (auto parts "A" and takes "R") in time order, in lines like text (the view scrolls down as they fill it), with long pauses drawn short and labelled with their real length; double-click one to hear it, or load it onto the tracks; right-click one to give it a name, write notes about it, or delete it (it goes into a `deleted` folder, so you can get it back). While recording, type a note in the transport area and press Enter: it is kept at that moment and shown as a mark on the timeline. File → Project note… keeps a note for the whole project. File → **Export DAWproject…** writes the tracks, takes, notes, instruments (with their sound) and marks as a `.dawproject` file, the open exchange format that Bitwig Studio, Studio One, Cubase and other programs read. File → **Import DAWproject…** makes a new project from such a file (from those programs or from mini-daw): the tracks with their volume, mute and solo, the audio (each track's clips in one take, converted to mini-daw's sample rate), the notes, the VST3 instruments with their sound and the markers; what mini-daw cannot hold (effects, automation, pan …) is listed. File → **Export audio…** makes audio files to listen to or hand on: the song as Play plays it and / or recordings from the Timeline, as Ogg Vorbis, Opus, FLAC or WAV, one mix each or stems (a file per track), optionally all in one .zip with the MIDI files; instruments are rendered as Render does. Recordings are kept as FLAC after they end (lossless, about half the size of WAV; Settings → Recording…), and File → **Compact recordings…** does the same for older ones. mini-daw runs once per user: starting it again brings the open window to the front. The window reopens at the size and place it had when you closed it (also maximised; `remember = false` under `[window]` in `config.ini` turns this off). It also comes back in the state you left it: auto record on (also after a crash), the song position, the instrument windows and mini-daw's other windows. The **Terms** tab explains the words mini-daw uses (track, take, arm, overdub, preset, latency …), with a Find field. Adjustable contrast and text size. Keyboard: Tab and the arrow keys move between controls, Enter presses the one in focus (Space stays Play/Stop). See `docs/planning/` for the Spec, Plan and test cases.

**New to mini-daw?** Start with [Getting started](docs/Getting-started.md): four short lessons (set up and record, MIDI and an instrument, overdub and mix, save and share). The same lessons are in mini-daw under **Help → Getting started**, which opens by itself the first time.

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

### macOS (Xcode or its Command Line Tools)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --parallel "$(sysctl -n hw.ncpu)"
ctest --test-dir build --output-on-failure
open build/src/mini-daw.app
```

Leave out `CMAKE_OSX_ARCHITECTURES` to build only for the Mac you are on. The command-line options work with the program inside the app: `build/src/mini-daw.app/Contents/MacOS/mini-daw --list-devices`.

### Cross-compiling for Windows from Linux (MinGW-w64)

```
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIDAW_BUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake
cmake --build build-win
```

### Command line

```
mini-daw                     open the GUI (Windows: mini-daw.exe; in a terminal mini-daw.com runs, see below)
mini-daw --config <file>     use this config file instead of the default one
mini-daw --list-devices      print audio devices and MIDI ports, then exit
mini-daw --audio-test <sec>  open the configured audio devices, print settings and levels
                             (add --monitor to hear inputs 1+2 and print the output level)
mini-daw --record-test <sec> <folder>
                             record input 1 (and 2) to a 24-bit WAV file in <folder>
mini-daw --contrast-report   print every colour pair and its contrast, then exit
mini-daw --version           print the version and build, then exit
mini-daw --script <file>     open the window, do what the file says, write <file>.report.txt, quit
                             (for tests; mini-daw --help lists the commands)
mini-daw --remote            let other programs send the same commands over HTTP for this run
```

A script is a text file with one command per line, the same actions as in the window, plus checks. For example:

```
# Load the instrument on a new track and check it
scan
add-track midi
instrument last "DX7 V"
expect log.errors == 0
```

The report lists each line as `ok` or `FAIL`, with the log lines it caused. The exit code is 0 when every line worked, 1 otherwise, 2 for a script with mistakes (nothing is run), 3 if mini-daw is already running with that config (use `--config` with another folder). Examples are in `tests/scripts/`.

### Remote control (HTTP + JSON)

With `mini-daw --remote` (for one run) or `enabled = true` under `[remote]` in the config file, another program can send mini-daw the same commands, one at a time, and read its state. The bottom of the window says when this is on. The port is 7800 unless `[remote] port` says otherwise.

```
curl http://127.0.0.1:7800/api/state                     # mini-daw's state as JSON
curl http://127.0.0.1:7800/api/commands                  # the commands, and which you may send
curl -X POST -H "Content-Type: application/json" \
     -d '{"line": "add-track midi"}' http://127.0.0.1:7800/api/command
# -> {"ok":true,"line":"add-track midi","note":"track 2","seconds":0.0001,"log":["... Track 2 \"MIDI 2\" added"]}
```

A command answers when it is done, so `instrument` waits until the plugin is loaded and `wait-for` until its condition holds or its time is up. `"ok": false` means the command ran and failed (the `note` says why). A second command sent while one runs gets 409; the state can be read meanwhile. `curl -X POST http://127.0.0.1:7800/api/cancel` ends a running `wait` or `wait-for` at once (also when the program that sent it goes away); `wait` is at most 600 s here.

Who may send commands:

- **This computer** may always send them, without a token. That means every program on it, also other users' and anything forwarded to it (an SSH tunnel): use remote control on a computer you do not share.
- **Other computers** may only send them from the networks in `[remote] allow`, for example `allow = 192.168.1.0/24`. They must also send the token with `-H "Authorization: Bearer <token>"`. mini-daw makes the token the first time and writes it into the config file (on Linux the file is then readable by you only).
- **Web pages** cannot use it. Requests with an `Origin` header are refused, the address has to be a number or `localhost`, and commands have to be sent as JSON.
- **`export`** (it can replace a file), **`import`** (it makes a project in any folder) and network paths (`\\server\share`, `//server/…`) are only accepted from this computer. `stay` and `stop-on-failure` are for script files only.
- **What a computer with the token can still do:** open any project on this computer (mini-daw then saves into it), record from your inputs (`arm`, `record`), load instruments, and everything else the window does with the open project. Its answers also tell whether a folder exists. Give the token only to computers you would let use mini-daw yourself.

This is plain HTTP: the token and everything else go over the network unencrypted, so only allow a network you trust. Windows asks once whether mini-daw may use the network when `allow` is set. Remote control is off while a script file runs.

## Screen contrast

The default theme is black text on a white background, and every colour combination on screen has a contrast ratio of at least 4.5:1 (WCAG AA). You can raise it:

- **In the program:** View → Contrast (AA 4.5:1, AAA 7:1, Max 21:1, or a custom value), or Ctrl+Shift+Up / Ctrl+Shift+Down. The choice is saved.
- **In the config file:**

  ```
  [ui]
  min_contrast = AAA   ; AA, AAA, Max, or a number from 4.5 to 21
  ```

  The file is `%APPDATA%\mini-daw\config.ini` on Windows and `~/.config/mini-daw/config.ini` on Linux.

A control under the mouse gets a frame, a pressed one also a darker fill, the chosen entry in a list a "> " mark, and a control that cannot be used now is flat with a dashed frame, so you can see what a click will do at every contrast setting, also at Max. View → Contrast report shows every colour pair and its ratio.

## Text size

View → Text size, or Ctrl+= / Ctrl+- / Ctrl+0, or `font_size = 20` under `[ui]` in the config file (12 to 32 px, default 16).

## Audio devices

Settings → Audio devices… selects the input and output device, sample rate and buffer size. The choice is saved under `[audio]` in the config file, by device name. If a saved device is missing, the program uses the system default and tells you so. To check the audio without the GUI:

```
mini-daw --audio-test 5
```

## Recording

The main window has a track list. *Add audio track* (one input, or two for stereo) and *Add MIDI track* (a MIDI input port, all channels or one). Tick *Arm* on the tracks to record, then press *Record* (or Ctrl+R); press it again to stop. Each recording goes into its own folder `take-YYYYMMDD-HHMMSS` inside the recording folder (Settings → Recording…; default `Documents\mini-daw` on Windows, `~/Music/mini-daw` on Linux), with one file per track: `01-Audio 1+2.wav`, `02-Keys.mid`, …

Audio is 24-bit WAV. MIDI is a Standard MIDI File; by default with 960 ticks per quarter note at a fixed 120 BPM (0.52 ms resolution, opens cleanly in any DAW). For free playing without a tempo you can switch to SMPTE timing (1 ms ticks) in Settings → Recording… or with `midi_timing = smpte` under `[recording]`. Both keep your timing exactly; PPQ only adds a bar grid that does not mean anything for unmetered playing. Tracks and takes are kept in a project (File → New project…).

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

## Installing

Download the files from the [Releases](https://github.com/maarten-boot/mini-daw/releases) page. `SHA256SUMS.txt` there has the checksum of each file (Windows: `certutil -hashfile <file> SHA256`; Linux: `sha256sum -c SHA256SUMS.txt --ignore-missing`; macOS: `shasum -a 256 -c SHA256SUMS.txt --ignore-missing`).

### Windows 10 / 11 (64-bit)

Run `mini-daw-<version>-windows-x64-setup.exe`. It installs for your user only, without administrator rights, into `%LOCALAPPDATA%\Programs\mini-daw`, with a Start menu entry (and, if you tick it, a desktop shortcut). Uninstall it in Settings → Apps; your settings, log, projects and takes stay. The installer is not signed yet, so Windows SmartScreen may warn about an unknown publisher: choose **More info → Run anyway**.

The folder has two programs: `mini-daw.exe` (the window, no console) and `mini-daw.com` (the same program for the terminal). In cmd or PowerShell, `mini-daw --list-devices` runs the `.com`, which prints its output and returns exit codes (for `--record-test`).

Without installing: unpack `mini-daw-<version>-windows-x64.zip` anywhere and start `mini-daw.exe`. Settings, log and plugin list are in `%APPDATA%\mini-daw` either way.

### Linux (x86-64)

The build is made on Ubuntu 24.04 and needs glibc 2.39 or newer (Ubuntu 24.04, Debian 13, Fedora 40 or later). It uses ALSA (MIDI, and audio) or PulseAudio / PipeWire's Pulse server (audio), X11 (also XWayland) and OpenGL. On Ubuntu / Debian these are usually there already; if not:

```
sudo apt install libasound2t64 libpulse0 libx11-6 libopengl0
```

Unpack and start it:

```
tar -xzf mini-daw-<version>-linux-x64.tar.gz
./mini-daw-<version>-linux-x64/mini-daw
```

Copy `mini-daw` to a folder in your PATH (e.g. `~/.local/bin`) to start it by name. Settings, log and plugin list are in `~/.config/mini-daw` (or `$XDG_CONFIG_HOME/mini-daw`); recordings go to `~/Music/mini-daw` unless you choose another folder. VST3 instruments are found in `~/.vst3`, `/usr/lib/vst3` and `/usr/local/lib/vst3`, and in folders you add (Settings → Instruments (VST®3)…). To remove mini-daw, delete the program and, if you like, those folders.

### macOS 12 or newer (preview)

`mini-daw-<version>-macos.dmg` (in the releases from 1.1.0 on; until then in the CI downloads as `mini-daw-macos`) holds one program for Apple silicon and Intel Macs. It is a **preview**: it is built and its automatic tests pass on every change, but nobody has tried it on a real Mac yet; please report what you find.

Open the `.dmg` and drag **mini-daw** onto **Applications**. mini-daw is not signed by Apple (there is no Apple developer account behind it), so the first start is refused: macOS says it cannot check mini-daw for malicious software. Then open **System Settings → Privacy & Security**, scroll down to the line about mini-daw and choose **Open Anyway** (and confirm). After that it starts normally. (In the Terminal, `xattr -dr com.apple.quarantine /Applications/mini-daw.app` does the same.)

When mini-daw first opens an audio input (usually at its first start), macOS asks whether mini-daw may use the microphone: allow it, or audio inputs record silence (mini-daw says so). You can change it later in System Settings → Privacy & Security → Microphone. As mini-daw is not signed by Apple, macOS may ask again after each update. Shortcuts are Cmd instead of Ctrl (Cmd+R to record, Cmd+Q to quit). Settings, log and plugin list are in `~/Library/Application Support/mini-daw`; recordings go to `~/Music/mini-daw`. VST3 instruments are found in `~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3`. To remove mini-daw, drag it from Applications to the Bin and, if you like, delete those folders.

## Known limits

- **Latency on Windows:** audio runs through WASAPI in shared mode, which adds some delay (exclusive mode and ASIO are not there yet). Use headphones and Monitor; overdubs are lined up with what you heard.
- **Windows 10 / 11 N editions** need the Media Feature Pack (`MFPlat.dll`), or mini-daw does not start.
- **macOS is a preview:** built and tested automatically, not yet tried on a real Mac. On Linux no JACK; the instrument's own window (Edit) needs X11 or XWayland.
- **VST3 instruments only:** no effects, no VST2 / CLAP / LV2 plugins.
- **No editing** of notes or audio; MIDI is recorded at a fixed 120 BPM grid (or SMPTE), without a tempo map.
- **Export audio:** no MP3 (its encoder is not under a licence mini-daw can use).
- **Not signed:** Windows SmartScreen may warn on the first start (More info → Run anyway); macOS refuses the first start until you choose Open Anyway (see Installing).
- **Max contrast (21:1):** hover and selection are shown by frames and marks instead of colours.

## Trademarks

VST is a registered trademark of Steinberg Media Technologies GmbH. mini-daw hosts VST3 plugins with the VST 3 SDK (MIT licence).
