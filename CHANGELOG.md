# Changelog

Every release of mini-daw, newest first. A release's section here becomes its notes on GitHub (`tools/release_notes.py`; the release job refuses a tag without one), so links in it are full URLs. Before 1.0.0 there were no releases: the builds were numbered `0.<step>.<round>` after the steps in `docs/planning/Plan.md`. From 1.0.0 on: `1.<minor>.<patch>`, the minor number raised by a step that adds something, the patch number by fixes.

## 1.2.2 — 2026-11-27

- **Connections first after an update.** With settings from an earlier version (1.1.0, or 1.2.0 already used once), the Connections tab sat at the right, before Terms, and mini-daw opened on the tab you used last, so the new tab was easy to miss. It now comes first and is shown once at the next start; after that mini-daw opens on the tab you used last again, and you can still drag the tabs into any order. (1.2.1 was built but not published: it did not move the tab for settings already saved by 1.2.0.)

## 1.2.0 — 2026-11-26

**A Connections tab for beginners, and colours per tab.**

### Connections
- A new first tab shows every input of your audio device (In 1, In 2 …) and every MIDI keyboard as a *socket*, with a cable to where it goes. Type the name of what is plugged in (a guitar, a microphone, a keyboard) and press Enter: that makes an armed track for it. **Unplug** removes it again (its recordings stay on disk).
- Audio goes straight to the recording (tick *stereo with In …* for a stereo pair); MIDI goes through an instrument you choose right there, with its channel next to the socket and **Edit** for the instrument's window.
- A socket whose device is not plugged in stays, greyed, with its name. With more than 8 inputs only In 1–8 and those in use show, until **Show all**.
- **Track details >** jumps to the track on the Recording tab (volume, mute, solo, presets, Render, its take), and **<** in front of a track there jumps back; the row you land on is marked, and so is the row under the mouse (a MIDI track together with its sound track).
- A stereo pair keeps its two inputs to itself, on both tabs; *Add audio track* takes an input nobody uses yet.
- See [Connections](https://github.com/maarten-boot/mini-daw#connections) in the README; Getting started now begins there.

### Look
- **Settings → Theme…**: your own background colour for each tab, the transport and the status line, so you see where you are. Text and buttons keep your contrast setting: on a dark colour the text turns white, and a colour no text could reach the minimum on is made a little lighter or darker. See [Colours per tab](https://github.com/maarten-boot/mini-daw#colours-per-tab).
- **Space between rows** (same window, or `space_unit` under `[ui]`): the Connections and Recording tables have room between their rows instead of lines; a MIDI track and its sound track stand closer together than other tracks.
- The MIDI channel of a track is now chosen on the Connections tab (the Recording tab shows it).
- New words in Terms: Connections, Socket, Stereo pair, Theme.

## 1.1.0 — 2026-11-22

**macOS, as a preview.** mini-daw now also comes as `mini-daw-1.1.0-macos.dmg`: one program for Apple silicon and Intel Macs, macOS 12 or newer. It is built and tested automatically on every change, but has not yet been tried on a real Mac, so tell us what you find. It is not signed by Apple: the first time, open it from System Settings → Privacy & Security → Open Anyway (see [Installing](https://github.com/maarten-boot/mini-daw#installing) in the README).

### On macOS
- Audio through CoreAudio and MIDI through CoreMIDI. macOS asks once whether mini-daw may use the microphone; the devices are opened again when you answer. Because the program is not signed, macOS may ask again after each update.
- VST3 instruments from `~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3`, with the instrument's own window (Edit). Presets in `~/Library/Audio/Presets`, where other Mac programs keep them too.
- Browse… opens the system's folder and file dialog.
- Shortcuts use Cmd instead of Ctrl (Cmd+R records, Cmd+Q quits); the menus, Terms, Getting started and the script help say so.
- Settings and the log in `~/Library/Application Support/mini-daw`; takes recorded without a project in `~/Music/mini-daw`.

### On every system
- Settings → Audio devices → Apply is refused, with a line saying why, while a recording, a Render or an audio export runs (it used to reopen the devices in the middle of that work).

### Known limits
As in 1.0.0, and on macOS: a preview, not yet tried on a real Mac; not signed (Open Anyway the first time); Opus export is slower on Apple silicon than it could be. See [Known limits](https://github.com/maarten-boot/mini-daw#known-limits).

## 1.0.0 — 2026-11-21

The first release. mini-daw is a small program to record MIDI and audio on several tracks at once, play it back, and play VST®3 instruments, for Windows 10 / 11 and Linux.

New to mini-daw? Start with **Help → Getting started** (it opens by itself the first time), or read [docs/Getting-started.md](https://github.com/maarten-boot/mini-daw/blob/main/docs/Getting-started.md).

### Recording and playing
- Audio and MIDI tracks, recorded together: Arm, Record (Ctrl+R), Stop. Audio as 24-bit WAV, MIDI as Standard MIDI Files with the exact timing you played (PPQ at 120 BPM, or SMPTE).
- Takes survive a crash or power loss: the file is kept valid and forced to the disk about once a second.
- Monitoring with level meters (dBFS, CLIP); overdub with what you hear lined up; playback with volume, mute and solo.
- **Compact recordings:** after a recording ends, its WAV files become FLAC (lossless, about half the size); each FLAC is checked sample by sample before the WAV is removed. File → Compact recordings… does older ones.
- **Auto record:** parts start when you play and end after a pause, each saved with its date and time.
- **Timeline** of every take and auto record part, in lines like text; play one, name it, add notes, load it onto the tracks, delete it.
- Notes while recording, at their moment in the take.
- MIDI keyboards may be unplugged and plugged in again.

### Instruments (VST3)
- Scan for VST3 instruments (with a cache; a plugin that fails or crashes is skipped from then on); one per MIDI track.
- The instrument's own window (Edit) on Windows and on Linux (X11), presets, its state kept in the project.
- The instrument's sound recorded live on a "… sound" track, or rendered from the MIDI take faster than real time; "Sound from" chooses which one plays.

### Projects and exchange
- Projects that save themselves and reopen as you left them (the last project, the song position, the tabs, the window).
- **Export audio:** the song and / or recordings, as one mix or stems, in Ogg Vorbis, Opus, FLAC or WAV, optionally in a zip with the MIDI files.
- **DAWproject** export and import, to and from other music programs (e.g. Bitwig Studio, Cubase, Studio One).

### Using it
- Readable for everyone: contrast presets AA (4.5:1), AAA (7:1) and Max (21:1, black on white) or your own minimum ratio; text size (Ctrl+= / Ctrl+-); keyboard navigation in every window.
- Tabs Recording, Timeline, Audio setup (in your order) and Terms (every word mini-daw uses, explained).
- One mini-daw per user; the diagnostics log under View → Log.
- For tests and other programs: scripts (`--script`), remote control over HTTP + JSON (`--remote`, off by default), `--record-test`, `--audio-test`, `--list-devices`.

### Known limits
See [Known limits](https://github.com/maarten-boot/mini-daw#known-limits) in the README: WASAPI shared mode only (no ASIO yet), no macOS, Linux needs X11 (or XWayland), only VST3, no note editing, no MP3, the programs are not signed.
