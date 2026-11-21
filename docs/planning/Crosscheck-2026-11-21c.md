# Crosscheck #27 — 2026-11-21 (after P31: compact recordings as FLAC)

Scope: Spec v0.76, Plan, Tests, README, Terms, CLAUDE.md, code at `e2f2f0d` (P31). Claude checked Spec ↔ Plan ↔ Tests ↔ docs. An independent reviewer read the code (`git diff 6e3dcef e2f2f0d -- src tests`): a subagent given the code, SPEC-REC-10, T-REC-36 and libFLAC's sources, but not Claude's conclusions. Claude then checked CC1 and CC2 in the code.

| Area | Verdict |
|------|---------|
| Spec ↔ Plan ↔ Tests | ✅ SPEC-REC-10, P31 and T-REC-36 agree. The setting, the menu, the window, `deleted` left alone, the float / > 8 channels / FLAC-already-there skips, the triggers (Stop, auto part, Render) and the script command are in the code |
| Scaling and reading | ✅ FLAC 16 / 24 / 32-bit give the same floats as `WavReader`. WAVE_FORMAT_EXTENSIBLE is read by its sub-format. Short reads set an error, and the Player plays silence for them. `seek_absolute` lands on the exact sample. ⚠️ a seek before every Player block is costly for FLAC **CC3**; a failed seek leaves the position wrong **CC8**; stale fields after `close()` **CC9**; a FLAC without a length reads as empty **CC12** |
| The check before the WAV goes | ✅ integer compare sample by sample, frames / channels / rate first, both readers closed before rename / remove, the FLAC removed again when the WAV cannot go. ⚠️ a WAV whose header is shorter than its data (a crashed take) loses its tail, and then the WAV is removed **CC1**; no sync to disk before the WAV is removed **CC4** |
| Compactor thread and GUI | ✅ state under the mutex, `cancel_` atomic, `stop()` joins before teardown, nothing added after `stop()`. ⚠️ a frame where a take path names a removed WAV; paths compared literally **CC5**; not paused in the same frame as `add` **CC6**; "already queued" by file name only **CC7**; `.part` files left by a crash **CC10**; script `compact` without a time limit **CC11** |
| Render | ⚠️ Render again after compaction: the new `-render.wav` gets the old name, sits beside the old FLAC and is never compacted **CC2** |
| Other readers of takes | ✅ Timeline, Load onto the tracks, Play it, the DAWproject export / import and Export audio accept `.flac`. Only text mentions of ".wav" are left **CC13** |
| Docs | ⚠️ `docs/Demo-checklist.md` expects a `-render.wav` after Render **CC13** |

## Findings

| # | Severity | Finding | Suggested fix |
|---|----------|---------|---------------|
| CC1 | high | `WavReader` uses the header's data size when it is smaller than the file (wav_reader.cpp:124 only corrects 0 or too large). The recorder refreshes the header every second, so after a crash or power loss the data size is too small. `compactFile` encodes and checks only those frames, both agree, and the WAV with the real tail is removed: up to about 1 s of a crashed take is lost for good. | `compactFile`: when the data chunk ends before the file does (beyond a pad byte), skip with "the WAV header is shorter than its data (an unfinished recording?); kept as WAV". Optionally let `WavReader` use the rest of the file when the data chunk is the last chunk. Test with such a WAV. |
| CC2 | medium | `newRenderPath` only tests `.wav` (track.cpp:54). After the first render was compacted, a second render reuses the name: a new WAV beside the stale FLAC, skipped by compaction ("FLAC there already") for good, and two takes with one number in the Timeline / Load onto the tracks. | Test both `.wav` and `.flac` of a name in `newRenderPath`. Test: render, compact, render again → `-render-2.wav`. |
| CC3 | medium | `Player::fill` calls `seek()` before every 4096-frame block (player.cpp:235); for FLAC each seek flushes the decoder and does a binary search. Many FLAC tracks, or high rates, may make the reader thread fall behind (not measured). | `FlacSource::seek`: nothing to do when `frame == position_`; inside the decoded buffer just move ahead. Test: sequential reads with `seek(position)` between them give the same samples. |
| CC4 | medium-low | The FLAC is closed by `encoder_finish` without a sync to disk, then the WAV is removed. Power loss soon after can leave neither file; the project's rule is that takes survive power loss (`syncToDisk`). | Reopen the part file and `detail::syncToDisk` before the rename (and the folder where possible). |
| CC5 | medium-low | Between `remove(wav)` in the thread and `stepCompactor` on the next frame, a track's take names a file that is gone: Play or a script then fails. `t.take == r.wav` is a literal comparison: a path written differently (relative, other case on Windows) never matches, so the track keeps a dead WAV path for the session. | A shared helper: a missing `.wav` take opens its `.flac` (as the project load already does), used by the Player and the other openers. Compare with `fs::equivalent` or `weakly_canonical`. |
| CC6 | low | `Compactor::add` wakes the thread at once; the pause only comes with `stepCompactor` in the next frame, so a file can start while Record / Play has just begun. Read-only, so only disk load. | `setPaused(compactorShouldWait(app))` before every `add`. |
| CC7 | low | `add` compares only the file name with `current_` (compact.cpp:195): `01-Vocal.wav` of another take folder is not queued while a file of that name is being converted. | Keep the full path in `current_` and compare paths. |
| CC8 | low | A failed FLAC seek (`flush` after `SEEK_ERROR`) leaves `position_` at its old value while the decoder is elsewhere: wrong audio without an error. | On failure `FLAC__stream_decoder_reset`, `position_ = 0`, and return false (or mark the source broken). |
| CC9 | low | `close()` / a failed open leave `frames_`, `rate_`, `bits_` of the last file; `readPcm` after `close()` uses a null decoder. Only by misuse. | Reset every field in `close()`; `read` / `readPcm` return 0 when not open. |
| CC10 | low | `.<name>.flac.part` files left by a crash or kill are never removed (hidden, only space). | Remove a stale `.part` of the same name before writing it. |
| CC11 | low | Script `compact` waits without limit while playback runs (other waits stop at 600 s); a WAV that cannot be read (8-bit) counts as failed every time. | The same 600 s limit; formats FLAC is not used for reported as skipped. |
| CC12 | low | A FLAC with an unknown length (`total_samples` 0, possible from other programs, e.g. a DAWproject import) gives 0 frames: silence. | Treat 0 as unknown: count the frames once by decoding, or read to the end. |
| CC13 | doc | Texts still say ".wav" only: Terms "WAV file" ("(.wav)" — it already mentions Compact), the "No takes (NN-name.wav or .mid)" message (track.cpp:283), the `timeline.hpp:24` comment; `docs/Demo-checklist.md` step 6 expects `-render.wav`. | "NN-name.wav, .flac or .mid"; the checklist says "`-render.wav` (FLAC soon after, with Compact on)", or goes with P32. |

Not taken up: the reviewer's doubt about the decoder's FILE after a failed `init_FILE` (it cannot fail with valid callbacks and an open file; the encoder side is as in the audio export, crosscheck #26 CC1).

Test coverage the reviewer misses (with the fixes above): a 32-bit WAV, WAVE_FORMAT_EXTENSIBLE, 3–8 and more than 8 channels, a 0-frame WAV, a WAV with a short header (CC1), Render after compaction (CC2), reads with `seek(position)` between them (CC3), a damaged FLAC.

## Actions taken (user: "yes commit, then fix CC1–CC13, CC1 first"; P31a, version 0.31.1)

| # | Done |
|---|------|
| CC1 | `WavReader`: a data size smaller than the file counts only when RIFF chunks follow it to the end (`chunksToEnd`); otherwise the data runs to the end of the file. A crashed take therefore plays and compacts with its last second. Test: header at 12 000 of 20 000 frames, so 20 000 read and compacted exactly; a WAV with a LIST chunk after the data is read by its header. |
| CC2 | `newRenderPath` also skips a name whose `.flac` is there. Test: `-render.flac` present gives `-render-2.wav`. |
| CC3 | `FlacSource::seek`: nothing to do when already at the frame; a frame inside the decoded buffer only moves ahead. Test: reads in 4096-frame blocks with a seek before each match the WAV; seeks inside the buffer, forward and back. |
| CC4 | The `.part` file is reopened and synced (`syncToDisk`) before the rename, and on Linux the folder is synced before the WAV is removed. A failed sync keeps the WAV. |
| CC5 | `AudioReader::open` (by the extension) opens the FLAC of a missing `.wav`, so a take works in the frame before the track is moved. `sameTakePath` (absolute, normalized, and on Windows any case and either slash) is used by `stepCompactor`. |
| CC6 | `queueCompact`: the pause is set (`compactorShouldWait`) before every `add`, from the recording / Render hooks, the window and the script. |
| CC7 | The Compactor keeps the whole path of the file it works on; "already queued" compares paths. Test: `01-Vocal.wav` in two folders, both compacted. |
| CC8 | A failed FLAC seek resets the decoder to the start (`position_ = 0`) and returns false. |
| CC9 | `close()` resets every field (FLAC and WAV); `readPcm` returns 0 when not open. Tested. |
| CC10 | `compactFile` removes `.*.flac.part` files in its folder first: only one file is converted at a time, so any found are left over. Tested. |
| CC11 | Script `compact` stops after 600 s ("it goes on in the background"). A WAV format mini-daw does not read (8-bit) is skipped, not failed. Tested. |
| CC12 | A FLAC whose header gives 0 samples is decoded once to count its frames. Test: a FLAC with its count zeroed reads all 40 000 frames. |
| CC13 | Terms "WAV file" (".wav, or .flac once compacted"), the "No takes (NN-name.wav, .flac or .mid)" message, the `timeline.hpp` comment, and `docs/Demo-checklist.md` step 6. |

Tests: Linux 277/277, Wine 257/257 (6159 assertions).
