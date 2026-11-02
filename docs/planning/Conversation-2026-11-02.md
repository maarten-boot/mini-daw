# Conversation log — 2026-11-02

Continues `Conversation-2026-11-01.md` (last section 29: Windows CI failure in T-REC-07, not fixed yet).

## 1. Linux build does not start on Ubuntu 24.04
> when starting on ubu24: ./mini-daw: /lib/x86_64-linux-gnu/libm.so.6: version `GLIBC_2.43' not found (required by ./mini-daw)

- Expected for `dist/linux/mini-daw`: it is built on this VM (glibc 2.43); Ubuntu 24.04 has glibc 2.39 (see `CLAUDE.md`, `dist/`).
- Downloaded the CI build of commit `aacc2ee` (run 41357305545, linux job passed) to `dist/linux-ubuntu24/mini-daw`; it needs at most GLIBC_2.38.
- Idea for later: stop putting the local build in `dist/linux/`, or name it after the glibc it needs, to avoid this mix-up.

## 2. Windows test failure fixed
> yes fix the windows test failure

- CI log of run 41357305545 confirmed it: both T-REC-07 tests fail at `readWav` while the writer still has the file open. No MSVC warnings.
- Cause: `_wfopen_s` (introduced in crosscheck #3 to avoid MSVC's C4996 on `_wfopen`) opens the file without sharing, so nobody else can read a take while it is being recorded. Wine does not enforce this, so the Wine run passed.
- Fix: `_wfsopen(path, mode, _SH_DENYWR)` — others may read, not write. The tests now show the error text on failure.
- Linux 65/65; MinGW under Wine 64/64 (Wine cannot show the problem, so CI on real Windows is the check).

## 3. CI green
> pushed it, check the CI run

- Run 41432521646: all jobs pass; 65/65 on Windows (both T-REC-07 cases now pass) and Linux; no MSVC warnings; both downloads present. Crosscheck #3 is closed.
- Still waiting for the user: T-REC-10 (`--record-test` with a working input on Windows), T-UI-03/04 on Windows. Next step: P6.

## 4. P6 started (stopped mid-way: "going home")
> start P6

- User decisions: takes go to a **recording folder** (`[recording] folder`, default `Documents\mini-daw` / `~/Music/mini-daw`), one subfolder per Record press; track list in memory only until P9. MIDI timing **switchable**: `midi_timing = ppq` (default, 960 PPQ at 120 BPM) or `smpte` (1 ms ticks). User asked whether SMPTE suits free improvisation without tempo: answered that both keep exact timing (PPQ is even finer, 0.52 ms); PPQ only adds a meaningless bar grid in a DAW, while SMPTE is handled badly by some DAWs, so PPQ stays the default.
- MIDI test plan (all chosen by the user): Linux VM sequencer access + a virtual keyboard, USB MIDI keyboard on Windows, a virtual port on Windows.
- Done, **uncommitted, not yet compiled**: Spec v0.11 (REC-01a, 02a, 04a–c, 07; REC-06 extended), Tests T-REC-16..22, Plan rows P6a/b/c; `src/core/include/minidaw/smf.hpp` + `src/core/src/smf.cpp` (writer via tmp + rename, `buildMidiTake`, reader); `src/core/src/file_util.hpp` (shared open-with-sharing / sync), `wav.cpp` switched to it.

## Handover (state at the end of 2026-11-02)
**Next steps for P6a**, in order:
1. Add `core/src/smf.cpp` to `src/CMakeLists.txt`; build; check that `wav.cpp` still passes after the `file_util.hpp` change.
2. Config: `RecordingSettings` gets `folder` (string, empty = default) and `midiTiming`; parse/save; default folder via `SHGetKnownFolderPath(FOLDERID_Documents)` on Windows, `$HOME/Music/mini-daw` elsewhere.
3. Timeline: `AudioStream` counts frames and publishes (frame, steady-clock ns) through a seqlock; `frameAtTime()`; pass the block's frame to `Recorder::process`, which remembers the first recorded frame (`startFrame()`).
4. `midi_input` (RtMidiIn per port, callback stamps steady-clock ns into an SPSC ring, `capture` flag) and `session` (take folder + names, starts audio Recorder, MIDI thread maps messages to frames, rewrites each `.mid` once a second, final write with note-offs at Stop).
5. Tests T-REC-16..20, then P6b (GUI, `--record-test` through the session), then P6c.
**Waiting for the user:** Linux VM MIDI access: `sudo usermod -aG audio mboot`, then log out and in again (`/dev/snd/seq` is root:audio). Optionally install a virtual keyboard (e.g. `sudo apt install vmpk`). Still open from before: T-REC-10, T-UI-03/04 on Windows.

## 5. P6a and P6b done
> continue

- MIDI access on the VM still missing (user not yet in group `audio`), so P6c waits.
- P6a (core): `smf` (writer via tmp + rename, reader, `buildMidiTake`), `frame_clock` (seqlock: audio callback publishes end-of-block frame + steady-clock time), `midi_input` (RtMidiIn per port, ring per port, capture flag; `RtMidiIn` declared last in `Port` so its thread stops before the ring is destroyed), `track` (names, take folders), `recording_session` (audio Recorder + MIDI thread, MIDI file rewritten each second, final write with note-offs). Config: `[recording] folder`, `midi_timing`. Quoted config values now keep plain backslashes (`"D:\Takes"` used to lose them).
- P6b (GUI): Recording section (Record/Stop, Ctrl+R, elapsed time, lost frames, last take), track table (Arm + "armed" text, name, input, level/activity, Remove), Settings → Recording…; the 32-line input-level list is folded away (crosscheck #2 G3). `--record-test` now goes through the session and also records every MIDI input. Added `imgui_stdlib.cpp` (part of Dear ImGui) for text fields.
- On the way: a `pkill -f` pattern matched its own shell and killed it; use `pkill -x mini-daw`.
- Results: Linux 76/76; Wine 75/75 Catch2 cases; GUI recording under Xvfb works (audio).
- Next: P6c with real MIDI (needs `sudo usermod -aG audio mboot` + re-login on the VM; Windows: USB keyboard / virtual port). Then crosscheck #4.

## 6. CI after P6a/P6b
> pushed it, check the CI run

- Run 41470756957: all jobs pass; 76/76 on Windows (MSVC, no compiler warnings) and Linux; both downloads present. The Windows-only code (`SHGetKnownFolderPath`, `_wfsopen`, `_commit`) builds with MSVC.
- Next: P6c (real MIDI), then crosscheck #4.

## 7. MIDI tested on the Linux VM (P6c, Linux part)
> i added myself to the audio group, test MIDI now

- The running shell does not have the new group yet (needs a new login); `sg audio -c ...` works without one.
- No virtual keyboard installed; used ALSA's "Midi Through" port (14:0) and `aplaymidi` with a generated test file: 10 notes 500 ms apart, 100 ms long, plus one controller. Scripts in the session scratchpad (`miditest/make_mid.py`, `analyse.py`).
- `--record-test`: all 21 messages recorded, 0 lost; with a 256-frame buffer note spacing within 1 ms. SMPTE timing and a note held at Stop work.
- GUI (Xvfb): armed audio + MIDI track, Ctrl+R, both files written, MIDI activity shown.
- Finding: with a 1024-frame buffer (the ALSA default device chose it) the spread grows to about ±10 ms, one note-off 21 ms early — within the Spec's "one buffer", but it comes from jitter of the audio callback time used as the anchor. Proposal: smooth the anchor (callbacks can only be late, so keep the smallest observed "clock − frame/rate" offset, letting it creep up slowly for clock drift). Waiting for the user's go.
- Also seen: with direct ALSA (now that the user is in `audio`) the driver reports 0.0 ms latency.

## 8. MIDI clock smoothing
> yes do the timing fix

- `ClockSmoother` (in `frame_clock.hpp`, audio thread only): smallest "callback time − frame/rate" of the last 0.5–1 s (two 0.5 s windows), raw time for the first 0.5 s after a start, restart on a dropout or a jump of > 50 ms / 3 buffers.
- First idea (keep the minimum, let it creep up 200 ppm for drift) replaced before testing live: a start-up burst would pin it too low for about a minute. The windowed version still left the first note 16 ms late in a live run (burst remembered for up to 2 s) → 0.5 s warm-up and 0.5 s windows.
- Result with a 1024-frame buffer: gaps within 0.52 ms instead of 9–16 ms; 256 frames: within about 1 ms. Unit tests with simulated jitter, burst, drift and dropout. Linux 78/78, Wine 77/77.
- Next: commit; Windows MIDI tests by the user; crosscheck #4.
