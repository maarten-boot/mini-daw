# Crosscheck #26 — 2026-11-21 (after P29: File → Export audio)

Scope: Spec v0.74, Plan, Tests, README, THIRD_PARTY.md, CLAUDE.md, code at `27e3d8f` (P29). Spec ↔ Plan ↔ Tests ↔ docs by Claude. The code (`git diff e7d4150 27e3d8f -- src tests cmake`) was also read by an independent reviewer: a subagent given the code, SPEC-PRJ-06 and the libraries' sources, not Claude's conclusions. Claude checked CC1 and CC2 in the code and FLAC's sources.

| Area | Verdict |
|------|---------|
| Threads and steps (`AudioExport`) | ✅ renders only on the GUI thread; the worker touches its own data and `result_` / `stage_` under the mutex; Cancel works at every stage; `done_` release / acquire; the window can be closed meanwhile; AppState order safe. ⚠️ no exception guard in the worker **CC5**; quitting while an export runs **CC7** |
| Encoders | ✅ Opus callbacks and drain, the Vorbis loop and EOS, the WAV / FLAC 24-bit conversion. ⚠️ FLAC closes the file twice after a failed init **CC1**; NaN / Inf samples reach Vorbis / Opus **CC5** |
| Mixing (`mixParts`) | ✅ block arithmetic, negative starts, mono / more channels, rate check, stems as long as the item, progress total |
| Files written | ⚠️ written in place: a failure or Cancel can remove a file that was there before, or the ones just replaced **CC2**; the Replace question misses recording stems and numbering **CC3**; Cancel racing with the zip's move **CC6**; output = input, names differing only in case, duplicate zip entries **CC8**; WAV's 4 GB limit only found at the end **CC10** |
| Items from the tracks | ✅ "Sound from", linked tracks, mute / solo in a mix, volume only in stems, files without a track at 0 dB. ⚠️ the song's start ignores muted tracks: a mix and its stems do not line up **CC4**; recording file names: any leading number, extension case **CC8** |
| GUI / script / remote | ✅ remote rule and its test; script completion order. ⚠️ an export during Render; an empty folder left after a refused start **CC9**; script options in other case, `zip` without a name, recordings named "song" / "all" **CC11** |
| CMake / licences | ✅ FORCE'd options do not reach other libraries; BUILD_TESTING not leaked; MinGW-only switches scoped; nothing GPL / LGPL linked |
| Spec ↔ Plan ↔ Tests ↔ docs | ✅ defaults (Ogg 6, Opus 160, ranges), THIRD_PARTY ↔ Dependencies (5 + 5), Plan P29 / P31, T-PRJ-12. ⚠️ SPEC-PRJ-06 points to §7 for MP3; it belongs in §6 Licensing **CC12** |

## Findings

| # | Sev | Finding | Proposed fix |
|---|-----|---------|--------------|
| CC1 | medium | `FLAC__stream_encoder_init_FILE` stores the FILE in the encoder before the step that can fail (`stream_encoder.c`: `private_->file = file`, then `init_stream_internal_`). On a failed init the encoder owns it, and `~FlacEncoder` closes it again: a double `fclose` (undefined behaviour; a crash). E.g. a sample rate FLAC refuses. | `file_ = nullptr` right after init, whatever its result. A test with a refused rate. |
| CC2 | medium | Files are written straight into the target folder, and a file is added to the cleanup list before it is opened. (a) An existing `song.ogg` that cannot be opened (read-only, locked on Windows) is removed by the cleanup. (b) After Replace, a failure or Cancel at item 2 removes item 1's new file: the old files are gone and no new ones are there. | Write every output into the work folder (as the zip already is) and move them into place only when all succeeded; failure / Cancel then only clean the work folder. |
| CC3 | low–medium | The window's "N file(s) are there: Replace" check makes recordings up with one empty part: for stems it looks for "<rec> - 00-.ogg", so real stems are replaced without the warning; " (2)" numbering and skipped linked tracks are not counted. | One naming function used by the export and by the check, on the real items. |
| CC4 | low–medium | `songExportItem` leaves a muted / not-soloed track out of a mix before taking the earliest start; stems keep it. A mix then starts later than its stems, so they do not line up. | The start from every track with a take, whatever its gain and the mode. |
| CC5 | low–medium | `runMix` has no try / catch: an exception (out of memory, `std::random_device`, a huge `resize` for a WAV header with very many channels) ends the program. NaN fails both tests in `clampSample` and goes on to Vorbis / Opus; `max(peak, NaN)` hides it, Inf prints "+inf dBFS". | try / catch around the worker (error, `done_` always set); NaN → 0 and counted as clipped. |
| CC6 | low | If Cancel comes after the zip was moved into place, the result says "Cancelled: nothing was written" while the zip is there (maybe over an older one); a failed copy (other drive) leaves a partial zip. | After a successful move, cancel no longer counts; a failed copy removes the partial file. |
| CC7 | low | Quitting while an export runs: nothing stops it before the instruments, audio and the window system are shut down; the `AudioExport` destructor then unloads its plugin after `glfwTerminate`. | Cancel and finish a running export before the shutdown steps. |
| CC8 | low | Recording files: `atoi` takes any leading number ("5 drums.wav"); `substr(3, …)` underflows for short names; `.Wav` / `.MID` are skipped. An output path equal to an input (exporting into a take folder with a matching name) truncates the source. Names differing only in case write one file on Windows. Two parts with one `.mid` name give duplicate zip entries. | "NN-" required, extension any case, guarded substr; skip / rename an output equal to an input; case-insensitive name sets; zip names numbered. |
| CC9 | low | An export may start while Render writes (or has just swapped) a linked track's WAV. A refused start leaves the folder it made. | Refuse while a Render runs (button disabled, the reason); make the folder only when the export starts. |
| CC10 | low | WAV stops at 4 GB (about 4.1 h at 48 kHz stereo 24-bit) with "Not written" only after the whole render. | Refuse such an item up front with a clear line (WAV: choose FLAC). |
| CC11 | low | Script: options in other case (`STEMS`, `Song`) are refused or taken as a recording name; `zip` at the end without a name says "unknown option"; a recording named "song" / "all" / "recordings" cannot be chosen by name. | Options in any case; "zip needs a name"; document that those names mean the keywords. |
| CC12 | doc | SPEC-PRJ-06 says MP3 is left out "(§7)"; §7 is Build and CI and has no such line. | A line in §6 (SPEC-LIC-02): MP3 left out because LAME is LGPL. |

Not taken up: `exportSafeName("x.zip")` gives "x.zip.zip" (as typed); work folders left by a crash (they are hidden and harmless; could be removed at the next export); very long Windows paths fail with a generic "cannot write"; FLAC's final `fclose` result inside libFLAC is not checked (the finish result is).

## Actions taken (user: "yes fix CC1–CC12, CC1 first"; P29a, version 0.30.5)

| # | Done |
|---|------|
| CC1 | `file_ = nullptr` right after `FLAC__stream_encoder_init_FILE`, whatever its result (libFLAC owns the FILE from there, as its own comment says). Test: rates above `FLAC__MAX_SAMPLE_RATE` (1 100 000, 2 000 000 Hz; 0 means "unknown" and is allowed). |
| CC2 | Every file is written into the work folder (`out/`), and moved into the chosen folder only when all succeeded. Test: two files there before, item 2 fails, both still have their old content, no work folder left. |
| CC3 | `exportOutputs`: one naming plan ("(2)", "(3)" …) used by the export and by `exportTargets`; the window builds the real items with `readFiles = false` (no file opened) for its Replace count, also for recording stems. |
| CC4 | `songExportItem` takes the start from the earliest take of any track; the tests now expect the muted guitar's start. |
| CC5 | `runMix` wraps the work (`mixAll`) in try / catch: an error result, `done_` always set. `mixParts` turns NaN / Inf into silence and counts them; the result names the file. Test with NaN and Inf gains. |
| CC6 | Once moving starts, Cancel no longer counts; a copy across drives goes to `.<name>.part` beside the target first and is renamed, a failed copy removed. |
| CC7 | Closing mini-daw cancels a running export and ends it (steps until done, finish) before the plugins, audio and window go; logged. |
| CC8 | `parseTakeFileName` (track.cpp, 1–4 digits and "-", the extension in any case) shared by Load onto the tracks and the export; names and zip folders compared in any case; MIDI names in a zip numbered; a target that is one of the export's own files refused in `start()`. Tests for all; Replace (the same export twice) also under Wine. |
| CC9 | No export while a Render runs (window line, button disabled, `startAudioExport` refuses); a folder made for an export that does not start is removed again. |
| CC10 | Before a WAV is written: over 4 GB (`WavWriter::kMaxDataBytes`) refused with "choose FLAC". Test with a part 800 M frames late. |
| CC11 | `export-audio` keywords and options in any case; `zip` without a name: "zip needs a name: zip <name>"; the command's help says a recording called song / all / recordings is meant as that word. Script case extended. |
| CC12 | SPEC-LIC-02: MP3 left out (LAME is LGPL), only libFLAC built and linked. |
| — | `zip_writer.hpp`: `_wfsopen` and `std::error_code` messages instead of `_wfopen` / `strerror` (MSVC C4996, 6 of the 25 warnings in CI run 44905865126). |
