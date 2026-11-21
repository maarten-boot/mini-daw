# Changelog

Every release of mini-daw, newest first. A release's section here becomes its notes on GitHub (`tools/release_notes.py`; the release job refuses a tag without one), so links in it are full URLs. Before 1.0.0 there were no releases: the builds were numbered `0.<step>.<round>` after the steps in `docs/planning/Plan.md`. From 1.0.0 on: `1.<minor>.<patch>`, the minor number raised by a step that adds something, the patch number by fixes.

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
