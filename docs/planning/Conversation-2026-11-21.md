# Conversation log — 2026-11-21

## 1. Commit P30; P30a: the transport's line area 4 lines

User (late on 2026-11-20): "yes commit, then shrink the transport area to 4 lines".

- P30 committed as `a918e0d` (version 0.30.0).
- P30a (version 0.30.1): the line area under the transport buttons is 4 lines high instead of 8 (SPEC-UI-10). It still scrolls when it needs more.
  - The playback notes ("the disk was too slow", files not played) now come after the other lines. Otherwise they could push the "Note at" line out of view while recording.
  - While recording, the 4 lines hold the RECORDING line, the "Note at" line and the take folder (2 lines when long).
- Linux 257/257. GUI on Xvfb (1150×750): the tabs moved up by about 80 px; while recording everything fits in the 4 lines.
- Spec (status v0.70, SPEC-UI-10), Plan P30a, Tests T-UI-34, README.

## 2. CI run for P30 / P30a; crosscheck #25

User: "pushed it, check the CI run", then "meanwhile do crosscheck #25".

- CI run 44834769758 (`ed4703f`) started; result below when it ends.
- Crosscheck #25 in `Crosscheck-2026-11-21.md`: Spec / Plan / Tests / docs by Claude, the code also by an independent reviewer (a subagent). Findings CC1–CC5:
  - CC1 (medium): a cut recording's last piece can be a few pixels wide, and ImGui then draws it across the line.
  - CC2: floating point at the last cut.
  - CC3: with 4 lines, a disk error while recording is below the visible part.
  - CC4: gap markers at a line's end / in very narrow windows.
  - CC5: stale lines in Tests.md and CLAUDE.md.
- Plan PX row: #25.
- CI run 44834769758 (`ed4703f`): windows, linux, licence green; Linux 257/257, Windows 242/242, installer built.

## 3. P30b: crosscheck #25 fixes

User: "yes fix CC1–CC5, CC1 first".

- CC1 / CC2 in `wrapTimeline`:
  - The last piece of a cut recording is at least `minPiece` wide (the cut before it moves back).
  - It ends exactly at the block's end (`to = 1.0`).
  - The GUI draws every piece at least 1 px wide.
- CC3: under the transport, "Not recording", the state line, errors and lost counts come first, then the note field.
- CC4:
  - A gap goes to the next line together with the block after it.
  - A line is at least as wide as a gap or a block's minimum width; a narrower window scrolls sideways.
  - Also: the "From …" text is now under the zoom slider (in a 700 px window it was a tall narrow column beside it).
- CC5: Tests T-UI-17 / T-PLAY-18, CLAUDE.md's version example.
- Version 0.30.2.
- Linux 259/259; GUI checked on Xvfb at 700 px.
- P30b committed as `46d8e2e` (with Crosscheck #25); not yet pushed at the end of the day.

## 4. P30c: Record disabled until a track is armed

User: "one note: thw transport can only become active after at least one track is armed , right ?"

- Read as "the". Answer: only Auto record needed an armed track.
  - Record was usable without one; it then refused with "No track is armed".
  - Play, Home and the position slider need a running stream and a take, not an armed track.
- User: "yes disable Record until a track is armed".
- Done (P30c, version 0.30.3, not committed):
  - The Record button is disabled until a track is armed, with the tooltip "Arm a track first".
  - Ctrl+R and the script command `record` keep their refusal line.
  - The Terms text "Arm" mentions it.
  - Spec SPEC-PLAY-01a, Plan P30c, Tests T-PLAY-22, README.
- Linux 259/259. Not yet checked in the GUI on Xvfb (user stopped for the day).

## Handover (end of 2026-11-21)

User: "we stop for today; tomorrow we continue".

- On `main`: P30 `a918e0d`, P30a `ed4703f` (pushed, CI 44834769758 green), P30b `46d8e2e` (committed, not pushed).
- P30c is in the working tree, not committed.
- Next:
  1. Check P30c on Xvfb: Record greyed out with its tooltip, enabled after Arm, Ctrl+R's refusal line.
  2. Commit P30c.
  3. The user pushes P30b + P30c; check the CI run.
  4. Empty `dist/` and put that run's Windows and Linux builds there, with `dist/TESTS.txt` for T-PLAY-21, T-PLAY-22, T-UI-33, T-UI-34. `dist/` still holds 0.28.4.
- Planned afterwards: P29 (export audio; open questions in the Plan). When done after P30 it gets version 0.30.<next round>.

## 5. Continued: P30c checked and committed

User: "continue".

- GUI on Xvfb: with no track armed, Record is greyed out with the tooltip "Arm a track first (tick Arm in the track list)."; Ctrl+R gives the refusal line; after Arm, Record and Auto record are enabled.
- Seen: the refusal line stayed after arming. Now it is cleared as soon as a track is armed. The text is a shared constant, `kNoArmedTrackError` in `recording_session.hpp`.
- Linux 259/259. P30c committed (version 0.30.3).
- Next: the user pushes P30b + P30c; check CI; `dist/` with that run's builds and `TESTS.txt` (T-PLAY-21, T-PLAY-22, T-UI-33, T-UI-34).
- CI run 44839364429 (`46d8e2e`, P30b): windows, linux, licence green; Linux 259/259, Windows 244/244.

## 6. dist/ with the P30b build

User: "continue".

- P30c was not pushed yet (GitHub could not be reached from this session), so there is no CI build for it.
- `dist/` now holds CI run 44839364429 (`46d8e2e`, P30b, version 0.30.2), both builds from that run:
  - `windows/`: `mini-daw-0.30.2-setup.exe`, `mini-daw.exe`, `mini-daw.com`, plus `instrument-switch.txt` kept for T-RUN-05.
  - `linux/`: `mini-daw`.
- `dist/TESTS.txt` new:
  - T-UI-33, T-UI-34, T-PLAY-21 first.
  - A note that P30c (T-PLAY-22) comes with the next build.
  - Recording steps on Linux (the Windows VM has no audio input).
  - Then the earlier still-open tests, from P25b on.

## 7. CI run for P30c; dist/ with 0.30.3

User: "pushed it, check the CI run".

- CI run 44901738118 (`a9f37cd`, with P30c): windows, linux, licence green; Linux 259/259, Windows 244/244; installer `mini-daw-0.30.3-setup.exe`.
- `dist/` replaced with both builds of this run, plus `instrument-switch.txt`.
- `dist/TESTS.txt`: T-PLAY-22 added at the top; the "not in this build yet" note removed.

## 8. P29 started: export audio, decisions

User: "start P29".

- Asked:
  - per file: one mix per take / stems / both / one file for all → **both, chosen per export**.
  - mix: **volume, mute, solo count**.
  - formats: **Ogg Vorbis, FLAC, WAV, Opus** (MP3 left out: LAME is LGPL).
  - zip: **audio + each take's own .mid files**.
- Then: Play plays "the song" (each track's take at its place, often from several recordings), the Timeline lists recordings. Asked what can be chosen → **the song and the recordings**.
- Claude's defaults (open to change):
  - Ogg quality 6; Opus 160 kbit/s at 48 kHz; FLAC / WAV 24-bit at the session rate.
  - Stems keep the volume but not mute / solo.
  - Files of another rate and MIDI without an instrument are left out and listed.
  - No overwriting without asking.
- Libraries (newest releases, BSD-3): libogg 1.3.6, libvorbis 1.3.7, FLAC 1.5.0, Opus 1.6.1, libopusenc 0.3.
- User, while P29 was being settled: "regarding manual export, we could also add a auto compact option to compact all wav files with a lossless compression so we save space on all recordings".
  - Read as "an automatic compact option".
  - Proposed as its own step P31 after P29, since every reader of a take must then read FLAC. Its details are settled later.
- Spec SPEC-PRJ-06 written out; Plan P29 "in progress", P31 planned.

## 9. P29 done: File → Export audio…

- Libraries (BSD-3, pinned by hash, in THIRD_PARTY.md):
  - libogg v1.3.6, FLAC 1.5.0 and Opus v1.6.1 with their own CMake.
  - libvorbis v1.3.7 and libopusenc v0.3 compiled from their sources.
  - FLAC's helper libraries (for its programs; partly GPL / LGPL) are not built.
- Core `audio_export`:
  - The four encoders write through files opened by mini-daw (paths with any characters on Windows).
  - `mixParts`: a mix or one stem; stems are as long as the item.
  - `songExportItem` / `recordingExportItem` apply the mixer and "Sound from" rules and list what is left out, with the reason.
  - `AudioExport`: renders on the GUI thread one after another (plugins load there), then a worker thread mixes, encodes and zips. The work folder is removed afterwards; Cancel removes what was made.
  - `ZipWriter` moved from `dawproject.cpp` to `zip_writer.hpp`, shared.
- GUI: File → Export audio…
  - The song, the Timeline's recordings (All / None), mix or stems, format and quality, zip with its name, folder with Browse….
  - "Replace" when files of these names are there; a progress bar and Cancel.
- Script command `export-audio`: over remote control only from this computer. Terms "Export audio" and "Stem".
- Seen while testing:
  - The MinGW programs suddenly needed `libssp-0.dll`, `libstdc++-6.dll` and `libwinpthread-1.dll`. Cause: Opus adds `ssp` with its FORTIFY / stack-protector options. These are off for MinGW only.
  - `file(READ … LIMIT 4)` gave 5 bytes of a binary file, so the check compares in hex.
  - The Ogg quality slider repeated its value.
- Tests:
  - T-PRJ-12: 6 cases, plus the script case `exportaudio` with its own setup (`run_script.cmake` now includes `<name>.setup.cmake`).
  - Linux 266/266; MinGW under Wine 247/247.
  - GUI checked on Xvfb.
- Version 0.30.4 (P29 after P30c raises the round). Spec v0.74, Plan P29 done, README, CLAUDE.md.
- Open: the user's Windows test with real instruments (T-PRJ-12 M); P31 (compact recordings as FLAC) to settle.

## 10. Commit P29; crosscheck #26

User: "yes commit, then do crosscheck #26"; then "pushed it, check the CI run".

- P29 committed as `27e3d8f`. CI run 44905865126 started (result below).
- Crosscheck #26 in `Crosscheck-2026-11-21b.md`, code read by an independent reviewer (a subagent). CC1 and CC2 checked by Claude in the code and in FLAC's sources.
  - CC1 (medium): FLAC closes the file twice after a failed init.
  - CC2 (medium): files written in place; a failure or Cancel can remove files that were there.
  - CC3: the Replace check for recording stems.
  - CC4: a mix and its stems do not line up when the earliest take is muted.
  - CC5: no exception guard; NaN.
  - CC6: Cancel against the zip's move.
  - CC7: quitting during an export.
  - CC8: file names (NN-, case, output = input, duplicate zip names).
  - CC9: export during Render.
  - CC10: WAV 4 GB limit.
  - CC11: script options' case.
  - CC12: the MP3 line belongs in Spec §6.
- CI run 44905865126 (`27e3d8f`, P29): windows, linux, licence green; Linux 266/266, Windows 250/250. MSVC C4996 warnings 25 (was 19): `zip_writer.hpp` now compiled in two files. `dist/` kept at 0.30.3 until CC1 / CC2 are fixed.

## 11. P29a: crosscheck #26 fixes

User: "yes fix CC1–CC12, CC1 first".

- CC1: FLAC owns the FILE from init on, also when init fails (its own comment says so). Test with rates above its maximum; 0 is allowed ("unknown"), so 1 100 000 and 2 000 000.
- CC2 / CC6:
  - Files are written into the work folder and moved into place only when all succeeded.
  - Moving ignores Cancel; across drives it copies beside the target first.
- CC3: one naming plan `exportOutputs`; the window's Replace count builds the real items without opening files.
- CC4: the song starts at the earliest take of any track (tests changed to it).
- CC5: try / catch around the worker; NaN / Inf become silence and are counted.
- CC7: closing mini-daw cancels a running export first.
- CC8: `parseTakeFileName` shared with Load onto the tracks; names in any case; zip MIDI names numbered; an export over its own source refused.
- CC9: no export during Render; a made folder removed when the export does not start.
- CC10: WAV over 4 GB refused up front.
- CC11: script words in any case, "zip needs a name".
- CC12: SPEC-LIC-02.
- Also: `zip_writer.hpp` without the calls MSVC warns about.
- While testing: a `git stash` / `stash pop` to build the old code was not needed; everything checked back in place, the stash empty.
- Linux 268/268; export tests under Wine 8/8 (with Replace). Version 0.30.5.

## 12. CI run for P29a; dist/ with 0.30.5

User: "pushed it, check the CI run", "continue".

- CI run 44908680287 (`0603de3`): windows, linux, licence green; Linux 268/268, Windows 252/252; installer `mini-daw-0.30.5-setup.exe`.
- MSVC C4996 warnings now 15 (19 before P29, 25 in P29): `zip_writer.hpp` no longer has them, also not in the DAWproject code.
- `dist/` replaced with both builds of this run, plus `instrument-switch.txt`.
- `dist/TESTS.txt`: T-PRJ-12 first (8 steps with real instruments on Windows), then P30–P30c and the earlier open tests.

## 13. P31 done: compact recordings as FLAC; P32 planned

User: "start P31".

- Asked:
  - when → **a setting + File → Compact recordings…**;
  - the WAV after the check → **removed**;
  - the setting by default → **on**;
  - DAWproject export → **FLAC as it is**.
- User, during the work: "do we already have a tutorial if not we sould plan one for beginners".
  - Read as "should". There is none: the Terms tab explains words, and `docs/Demo-checklist.md` is a presenter's checklist from 0.1.0.
  - Planned as P32 (form and lessons to settle after P31).
- Done:
  - Core `audio_reader`: one reader for WAV and FLAC with the same float scaling and FLAC seeking; every reader of a take uses it.
  - FLAC accepted by the take file names, the Timeline, Play it, the DAWproject export (FLAC as is) and import, and Export audio.
  - Core `compact`: `compactFile` writes `.part`, decodes and compares, renames, then removes the WAV, or undoes when it is in use. The `Compactor` thread is paused while files are in use.
  - `[recording] compact = true`. Hooks after Stop, an auto part's end and a Render.
  - File → Compact recordings… window.
  - Tracks moved to the FLAC and the project saved at once; a missing WAV take falls back to its FLAC on load.
  - Script command `compact`; Terms "Compact".
- Seen under Wine: every file failed with "Sharing violation". The WAV was still open in the encoder's reader when it was removed (Linux allows that), so it is now closed first.
- Tests: T-REC-36 (4 cases), script case `compact` with a WAV fixture. Linux 273/273, Wine 253/253. GUI on Xvfb: a recorded take became FLAC after Stop and played; the window's tick-box label was shortened.
- Version 0.31.0. Spec v0.76 (SPEC-REC-10), Plan P31 done, P32 planned, README, CLAUDE.md.

## 14. P31 committed; crosscheck #27; a release planned (P33)

User: "yes commit, then do crosscheck #27".

- Committed as `e2f2f0d`.
- Crosscheck #27 started: an independent reviewer reads the P31 code; Claude checks Spec ↔ Plan ↔ Tests ↔ docs.

User, during the crosscheck: "can we plan a release v0.8.0".

- No tags, no releases and no release job in CI yet.
- 0.8.0 would sort below the 0.31.0 builds already made. Asked:
  - version → **v1.0.0**;
  - publishing → **a CI job on a tag**, creating a draft release;
  - before the release → **crosscheck #27 fixes, the P32 tutorial, release docs** (the user's Windows tests were not chosen as a gate).
- Planned as P33.
- User: "Windows tests are not a release blocker" (noted in P33).

User: "meanwhile start the release CI job for P33".

- Job `release` in `build.yml`: only on a tag `v*`, after the windows, linux and licence jobs. It is the only job with `contents: write`.
  - It packs the setup, a Windows zip, a Linux tar.gz and `SHA256SUMS.txt`.
  - It makes a **draft** release with `gh release create --draft --verify-tag`; an `-rc` tag becomes a pre-release.
- `tools/release_notes.py`: the tag must be `v<x.y.z>[-rc<n>]` and equal the CMake version. The notes are the `## <x.y.z>` section of `CHANGELOG.md` (required for a real release) plus a downloads table. The licence job runs it first, so a wrong tag fails early.
- `actions/download-artifact` v8.0.2 pinned by hash. The pin check passes.
- SPEC-CI-05, T-CI-04. To test: push tag `v0.31.0-rc1`.

Crosscheck #27 done: `Crosscheck-2026-11-21c.md`, CC1–CC13. Claude confirmed CC1 and CC2 in the code.
- CC1 (high): a crashed take, whose WAV header is shorter than its data, loses its tail when it is compacted.
- CC2: Render again after compaction reuses the old name.
- Fixes planned as P31a.

User: "yes commit, then fix CC1–CC13, CC1 first".

- Committed as `ac7ad57` (release job and crosscheck #27).
- P31a done, CC1–CC13. See the actions table in `Crosscheck-2026-11-21c.md`.
  - CC1: a WAV whose header is too short, with no chunk after the data, is read to the end of the file.
  - The other fixes are as planned.
- 4 new test cases. Linux 277/277, Wine 257/257.
- Version 0.31.1, Spec v0.77.

## 15. CI run for 0.31.1; P32 started

User: "pushed it, check the CI run", "meanwhile start P32".

- CI run 44931493128 (`34f7f22`) green:
  - Linux 277/277, Windows 260/260; installer `mini-daw-0.31.1-setup.exe`.
  - `release` skipped, as it should be (no tag).
  - MSVC C4996 warnings: 15.
- `dist/` replaced with both builds of this run, plus `instrument-switch.txt`.
- `dist/TESTS.txt`: T-REC-36 M first (7 steps), then the P29 / P30 tests and the earlier open ones.
- P32: asked.
  - form → **both**: a Help → Getting started window, and a docs page made from the same text;
  - lessons → **all four**: set up and first recording, MIDI and an instrument, overdub and mix, save and share;
  - `docs/Demo-checklist.md` → **replaced** by the tutorial.
- P32 done (version 0.32.0, Spec v0.78, SPEC-UI-19, T-UI-35):
  - Core `tutorial`: 4 lessons, 21 steps. 18 of them tick themselves from what mini-daw sees (`TutorialFacts`).
  - Help → Getting started… window: it opens by itself at the first start.
  - `docs/Getting-started.md` is made from the same text; a test compares them.
  - Render and Edit are on the MIDI track's Output column, and the lesson text says so.
  - `docs/Demo-checklist.md` removed. The README links the guide.
  - Tests: Linux 280/280, Wine 260/260. GUI checked on Xvfb.

User: "pushed it, check the CI run", "meanwhile write the release docs".

- Release docs written for P33:
  - `CHANGELOG.md`: its `## 1.0.0 — not released yet` section becomes the release notes, so the links in it are full URLs.
  - README **Installing**: the Releases page, checksums, Windows installer and zip, and the Linux tar.gz with its packages and folders.
  - README **Known limits**.
  - The intro now says Windows 10 / 11 and Linux.
- The release job also packs `CHANGELOG.md`.
- The release-day steps are in the P33 row.
- CI run 44935373008 (`63de5da`) green:
  - Linux 280/280, Windows 263/263; installer `mini-daw-0.32.0-setup.exe`.
  - `release` skipped; C4996 warnings: 15.
- `dist/` replaced with both builds of this run, plus `instrument-switch.txt`.
- `dist/TESTS.txt`: T-UI-35 M first, then T-REC-36 and the rest.

User: "pushed the rc tag, check the release run".

- CI run 44938379062 (`80e7eba`, main) green.
- Tag run 44939335251 (`v0.32.0-rc1`): every job green, `release` included.
  - The tag check passed in the licence job.
  - Four files were made: setup, zip, tar.gz and `SHA256SUMS.txt`. A draft pre-release was created.
- This session's GitHub token is read-only and cannot see drafts. The user checks the draft, then deletes it and the tag.
- User: "draft looks fine, deleted it and the tag". T-CI-04 passes. The tag is gone from GitHub; no local copy.

User: "yes prepare 1.0.0 with today's date".

- `CMakeLists.txt` VERSION 1.0.0.
- `CHANGELOG.md` `## 1.0.0 — 2026-11-21`.
- README status line: "version 1.0.0, the first release".
- The version rule from now on is `<major>.<minor>.<patch>`: a step that adds something raises the minor number, fixes raise the patch number. This is written in Spec SPEC-UI-18, `CLAUDE.md`, `version.hpp.in`, the CHANGELOG intro and T-UI-32, whose pattern also allows the 0.x numbers before the release.
- Spec v0.79.

User: "pushed it, check the CI run", "pushed the v1.0.0 tag, check the release run".

- CI run 44941956581 (`4f9a9a5`, main) green.
- Release run 44942212889 (tag `v1.0.0`): every job green, `release` included.
  - Linux 280/280, Windows 263/263.
  - The draft "mini-daw 1.0.0" was made with setup 1.7 MB, zip 4.1 MB, tar.gz 3.2 MB and `SHA256SUMS.txt`; the notes are the CHANGELOG 1.0.0 section.
  - The user checks the draft and publishes it.
- `dist/` replaced with that run's builds (`mini-daw-1.0.0-setup.exe` under its build name), plus `instrument-switch.txt`; `TESTS.txt` header updated.
- User: "the release is missing the windows and linux assets".
  - The published `v1.0.0` release had been made by hand on GitHub at 19:34 (notes "a first", no files). The job's draft "mini-daw 1.0.0" stayed a draft, because a tag can hold only one published release.
  - The user deleted the hand-made release and published the draft.
  - Checked through the public release information: "mini-daw 1.0.0" with 4 files and the CHANGELOG notes; it is the only release. P33 done.
- User: "what about the mac version did we drop that along the way ?"
  - Not dropped: macOS was "later" from the start (Spec "Windows 10 first; Linux and macOS later"; Plan P12, which also holds JACK and Wayland).
  - Never built and not in CI. The README says "macOS later".
- User: "yes plan P34, no mac yet, unsigned is fine".
  - SPEC-TECH-07, macOS:
    - a universal `.app` (arm64 + x86_64) in a `.dmg`, macOS 12 or newer;
    - CoreAudio / CoreMIDI, the VST3 editor in an NSView;
    - the microphone permission;
    - unsigned, with how to open it the first time.
  - Plan:
    - P34a: a CI job on a macOS runner;
    - P34b: audio and MIDI;
    - P34c: VST3 and the editor;
    - P34d: packaging, the `.dmg` in the release, README;
    - P34e: a crosscheck, then release 1.1.0.
  - P12 now holds only JACK and Wayland.
  - Tests T-MAC-01 (automatic in CI) and T-MAC-02 to -04 (manual, once there is a Mac).
  - Without a Mac, a part counts as done when it builds and its automatic tests pass on the CI runner.

## 16. P34a started: a macOS CI job

User: "start P34a".

- No Mac here, so the Mac build can only be tried in CI. The first round is written from reading the code; the CI run will show what is left.
  - Job `macos` on `macos-15`, arm64 for now: configure, build, ctest, smoke run, upload `mini-daw-macos`.
  - `continue-on-error`, and the release does not wait for it.
  - CMake: OBJCXX on Apple. The SDK's `module_mac.mm` and `threadchecker_mac.mm` are built with ARC; CoreFoundation / Foundation are linked.
  - OpenGL 3.2 core, forward-compatible, with GLSL 150 on macOS.
  - The Mac VST3 folders: `~/Library/Audio/Plug-Ins/VST3` and `/Library/Audio/Plug-Ins/VST3`.
  - The test synth as a Mac bundle: `macmain.cpp`, `Contents/MacOS`, an `Info.plist`.
  - The script / remote tests stay Linux-only (they need Xvfb).
- Linux 280/280; the MinGW build is unchanged.

### 16.1 The first macOS run

User: "pushed it, check the CI run" (run 44954198465). Linux, Windows and the licence check passed; the job `macos` failed, the run stayed green (`continue-on-error`).

- Configure worked on the Mac.
- The test synth did not link: `Steinberg::Linux::IEventHandler::iid`, `ITimerHandler::iid`, `IRunLoop::iid` undefined. The SDK defines them only on Linux (`SMTG_OS_LINUX` in `commoniids.cpp`), and the code used them on every system but Windows.
  - Fix: the editor run loop code is `__linux__` only, in the host (`PlugFrameImpl`), the test synth, and T-VST-38 (macOS editors have their own message loop, as on Windows).
- The build took 50 minutes: `cmake --build --parallel` with Make and no number is a bare `-j`, every job at once on a 3-core runner. Now one job per core (`sysctl -n hw.ncpu`), and Make's `-k`: the first run stopped at the first error, so most of the code was never compiled; now one run shows every file that does not compile yet.
- Linux 280/280 again; the MinGW build is fine.
- `dist/` now holds the Windows and Linux builds of 24da29b (the same program as 1.0.0).
- The user meanwhile installed the Windows build with the setup exe (the zip has no installer): it works.

### 16.2 The second macOS run (2026-11-22)

User: "pushed it, check the CI run" (run 44959348477). Linux, Windows and the licence check passed; `macos` failed again, now after 2 minutes instead of 50.

- Three files did not compile, all for decimal numbers as text:
  - `dawproject.cpp`: `std::to_chars(double)` is only there from macOS 13.3 (we build for 12).
  - `script.cpp`, `theme.cpp`: Apple's libc++ has no floating-point `std::from_chars` at all.
- Fix: `minidaw/number_text.hpp`, `formatDouble` / `parseDouble`. They use the standard functions where the library has them (Linux, Windows) and a portable fallback elsewhere (stream in the "C" locale, shortest digits that read back, the same choice between fixed and scientific as `to_chars`).
- New test T-MAC-05: the fallback gives the same results as the standard on 4000+ values (it found one difference first: `to_chars` writes large whole numbers with their exact digits; fixed).
- Linux 281/281 (with Xvfb; the XSD check skipped as before); the MinGW build is fine.

### 16.3 The third macOS run: it builds

User: "pushed" (run 44962018436). Linux, Windows and the licence check passed. On the Mac the build succeeded (2.5 min) and 265 of 267 tests passed; the smoke run was skipped after the failures.

- T-UI-21b: the test makes a folder named in Latin-1 (not UTF-8) on purpose; APFS refuses such a name ("Illegal byte sequence"). On macOS the test now allows that refusal; elsewhere the folder must be made, as before.
- T-VST-37: two readers on one pipe. macOS's `poll()` (built on kqueue) marks a descriptor listed twice as ready on one entry only, so the first reader was never called. Fix in `RunLoop::poll`: each descriptor once in the poll list, every reader of it called. A new check: two readers of one descriptor are both called.
- Linux 281/281 with Xvfb (XSD skipped as before); MinGW build fine.

### 16.4 The fourth macOS run: green

User: "pushed it, check the CI run" (run 45012825655). All jobs green, `macos` included:

- Build, 267 / 267 tests, then the smoke run:
  - `mini-daw 1.0.0 (commit 7a1b2e2 of 2026-11-22, CI run 45012825655)`
  - Audio API CoreAudio, the runner's three virtual devices (Apple Virtual Sound Device in / out, Null Audio Device), no MIDI ports.
- `mini-daw-macos` uploaded (2.5 MB, the program for Apple silicon, not yet an `.app`).
- So P34a is done by its rule (builds and passes its automatic tests on the CI runner), for arm64; the universal build is part of P34d.
- Clang's `-Wconversion` also warns on sign changes, which GCC's does not: about 30 in our code (int → size_t, int16 → size_t), all harmless; one `__int128` → int64 for a file time in `plugin_scan.cpp` (libc++'s file clock; only compared for equality, fits). To be cleaned up with P34b. The OpenGL deprecation warnings are expected (SPEC-TECH-07).

## 17. P34b: audio, MIDI and the microphone permission (2026-11-22)

User: "yes commit, then start P34b" (P34a done committed as ff3c523).

- Already in place since P34a: RtAudio with CoreAudio and RtMidi with CoreMIDI; the smoke run listed the CoreAudio devices. The config, log and recording folders already had their macOS places (`~/Library/Application Support/mini-daw`, `~/Music/mini-daw`). MIDI port names are used as macOS gives them.
- New, the microphone permission:
  - macOS lets a program record only after the user allowed it; until then CoreAudio gives silence without an error.
  - `minidaw/mic_access.hpp`: `micAccess()` (AVFoundation, never asks), `askMicAccess()`, `micAccessAnswers()`, `micAccessWarning()`. Windows and Linux: `NotNeeded`.
  - `AudioStream::open` with an input asks when macOS has not asked yet, and adds a warning line when mini-daw may not record (refused: where in System Settings to allow it).
  - When the answer comes, the app opens the devices again (not during a recording; then after it).
  - macOS ends a program that asks without a usage text. So `res/macos/Info.plist.in` (with `NSMicrophoneUsageDescription` and the version) is built into the program with `-sectcreate __TEXT __info_plist`; P34d's `.app` uses the same file. A program without that text (the tests) does not ask.
  - CI step on macOS: the built-in plist is read back and must have the usage text.
- The clang warnings from P34a: about 25 sign changes fixed with explicit casts; `ComponentHandlerImpl` made `final` (clang: deleted through a `unique_ptr` without a virtual destructor); the 128-bit file time cast on purpose; an unused parameter on macOS marked. A local clang 21 build now has no warnings in our code.
- New test T-MAC-06. Linux 282/282 (Xvfb; XSD skipped as before), MinGW build fine.
- Version stays 1.0.0: the macOS work is released as 1.1.0 at the end of P34 (P34e).

### 17.1 The P34b run: green

User: "pushed it, check the CI run" (run 45016560178). All jobs green.

- macOS: 268 / 268 tests (T-MAC-06 included), the smoke run as before, and the new step read the built-in Info.plist: `org.minidaw.mini-daw 1.0.0` with the microphone usage text.
- No warnings in our code on macOS any more, except the expected OpenGL deprecation ones.
- P34b done as far as CI can check it; asking for the microphone and recording after it are for T-MAC-02 / -03 on a Mac.

## 18. P34c: the instrument's own window on macOS (2026-11-22)

User: "yes commit, then start P34c" (P34b done committed as 428f861).

- Already done in P34a: the Mac VST3 folders, `.vst3` bundles through the SDK's `module_mac.mm`, the test synth as a bundle; the VST tests passed on macOS.
- New, the editor window (`plugin_window.cpp` with the Cocoa calls in `plugin_window_mac.mm`):
  - a GLFW window without OpenGL, as on Windows and Linux; its content view goes to the plugin as "NSView";
  - once shown, a child window of the main window: it stays above it and is minimised with it;
  - no content scale on macOS: VST3 sizes are points there and the system scales;
  - plugin timers run on the main thread's run loop, which `glfwPollEvents` runs every frame.
- The test synth gets an NSView editor (`testsynth_mac.mm`): white with black text, a click on the left / right half lowers / raises the gain, as its Windows and X11 windows.
- New test T-MAC-07 (macOS only, Objective-C++): the editor in a Cocoa window that is never shown; size, resize, a click that reaches the sound, the view gone after close.
- `GL_SILENCE_DEPRECATION` for our OpenGL code on macOS (the warnings were expected).
- None of the Objective-C++ could be compiled here; Linux 282/282 (Xvfb, XSD skipped) and the MinGW build are unchanged.

### 18.1 The P34c run: green

User: "pushed it, check the CI run" (run 45018905401). All jobs green.

- macOS: 269 / 269 tests; T-MAC-07 passed on the first try (the test synth's NSView in a Cocoa window: size, resize, a click reaching the sound, gone after close). No warnings left in our code.
- P34c done as far as CI can check it; a real plugin's window is T-MAC-04 on a Mac.

## 19. P34d: the .app, the .dmg, one program for both kinds of Mac (2026-11-22)

User: "yes commit, then start P34d" (P34c done committed as 0b68c6d).

- `mini-daw.app`: CMake's MACOSX_BUNDLE with the Info.plist of P34b (now also `CFBundleIconFile`); `mini-daw.icns` made at build time with `sips` and `iconutil` from `res/mini-daw-256.png` (16 to 256 px).
- CI (job `macos`):
  - configure with `CMAKE_OSX_ARCHITECTURES=arm64;x86_64`; Opus is then built without its SIMD code (its CMake picks it from the build machine's processor, which does not fit both slices);
  - the tests run as arm64; `lipo -archs` must list arm64 and x86_64;
  - both plists (built in, and the app's) and the icon are checked;
  - the .app is signed ad hoc (`codesign --sign -`), so macOS sees one unchanged program and offers Open Anyway; no Apple signature (user's choice);
  - `hdiutil` makes `mini-daw-<v>-macos.dmg` (the app, a link to Applications, LICENSE, THIRD_PARTY.md, README.md, CHANGELOG.md), verified, uploaded as `mini-daw-macos`.
- Release job: `needs` includes `macos`, but its `if` only requires Windows, Linux and the licence check to have succeeded; the .dmg goes into the release when the macOS job made one, and `tools/release_notes.py --macos` adds its line and the note about Open Anyway. Tried locally with fake files, with and without a .dmg.
- Cmd instead of Ctrl: ImGui already treats its Ctrl modifier as the Command key on macOS, so only the texts change: `minidaw::keysHere()` in the menus, the transport, tooltips, the status line, Terms and Getting started; `docs/Getting-started.md` (the same text on every system) says "On a Mac, the shortcuts are Cmd instead of Ctrl" once. Test T-MAC-08.
- README: building on macOS; Installing → macOS 12 or newer (preview: drag to Applications, Open Anyway, the microphone question, Cmd shortcuts, folders); Known limits updated.
- Linux 283/283 (Xvfb, XSD skipped), MinGW build fine. The macOS part is tried by the next CI run.

### 19.1 The P34d run: green

User: "pushed it, check the CI run" (run 45028873440). All jobs green.

- macOS: build 4.5 minutes for both architectures (was 2.5 for arm64 alone); 270 / 270 tests; `lipo -archs`: x86_64 arm64.
- The built-in plist and the app's Info.plist: `org.minidaw.mini-daw 1.0.0`, icon `mini-daw`, the microphone usage text; the icon file is there.
- `codesign --verify --deep --strict`: valid on disk, satisfies its Designated Requirement (ad hoc).
- `mini-daw-1.0.0-macos.dmg`, 5.6 MB, checksum valid; the download `mini-daw-macos` holds it.
- P34d done as far as CI can check it. Left: P34e (crosscheck, release 1.1.0).

## 20. P34e: crosscheck #28 and its fixes (2026-11-22)

User: "yes commit, then start P34e" (P34d done committed as 00b6e58).

- Crosscheck #28 (`docs/planning/Crosscheck-2026-11-22.md`), done the same way as #27:
  - Claude checked Spec ↔ Plan ↔ Tests ↔ docs.
  - A reviewer subagent read the P34 code (`git diff cd917dc 00b6e58`) with SPEC-TECH-07, without Claude's conclusions.
  - Claude checked CC2 and CC7 in the code.
  - Findings: 12, none high: CC1 no time limit on the macOS job; CC2 reopening audio during Render / export (Apply had it before P34); CC3 presets in the Linux folders on macOS; CC4 no NSOpenPanel as the Spec says; CC5 the command-line tests and the microphone question; CC6 denormals in the parse fallback; CC7 / CC8 Ctrl in the Terms search and the script help; CC9 the test synth's view lifetime; CC10 / CC11 docs; CC12 Opus without SIMD on Apple silicon.
- User (asked): fix CC1–CC11, with CC4 as NSOpenPanel ("CC1–CC11 (Recommended)"); keep the release not waiting for the macOS job ("Keep it optional").
- All eleven fixed (see the crosscheck's actions). Linux and MinGW are checked here; the macOS parts (NSOpenPanel, the presets on macOS, the test synth) are tried by CI.

## 21. P34e: CI green, release 1.1.0 prepared (2026-11-22)

User: "yes commit" (b639eaf), "pushed it, check the CI run" (run 45041822344). All jobs green.

- macOS: 270 / 270 tests; `folder_chooser_mac.mm` (NSOpenPanel) built; T-VST-35 (Mac preset folders) and T-MAC-05 (the new denormal cases) passed; `.dmg` made.
- Linux 283 / 283, Windows 266 / 266.
- `dist/` replaced with this run's Windows and Linux builds (plus `instrument-switch.txt`); the `TESTS.txt` Build line names b639eaf.

User: "yes, prepare 1.1.0".

- `CMakeLists.txt` VERSION 1.1.0.
- CHANGELOG `## 1.1.0 — 2026-11-22`: macOS as a preview (CoreAudio / CoreMIDI and the microphone question, VST3 and the editor, Mac presets, the system dialog, Cmd, the folders), Apply refused during recording / Render / export, known limits.
- README status line: version 1.1.0, steps P2–P34.
- Plan: P34 and P34e done.
- `tools/release_notes.py --macos v1.1.0` gives the notes with the `.dmg` line.

User (meanwhile): where is the gh token kept and how to renew it. It is in `~/.config/gh/hosts.yml` (github.com, account maarten-boot), a token that expires 2026-11-30; renew with `gh auth login -h github.com --with-token` from the user's own terminal.

User: "pushed it, check the CI run" (run 45044604946, 6219305): all green, every build `mini-daw 1.1.0`; `dist/` replaced with its builds (`mini-daw-1.1.0-setup.exe`), `TESTS.txt` header for 1.1.0.

User: "pushed the v1.1.0 tag, check the release run" (run 45048508723): all jobs green, `release` included. The draft release holds `mini-daw-1.1.0-windows-x64-setup.exe`, `-windows-x64.zip`, `-linux-x64.tar.gz`, `-macos.dmg` (5.6 MB) and `SHA256SUMS.txt`; the notes have the macOS line. The draft is not visible to the read-only token; the user publishes it on GitHub.
- The user published the draft as release v1.1.0 ("published it").

## 22. P35: colours per area (2026-11-23)

User: "add a theme setting menu where the user can set the background color for each of the tabs and the transport area, best build a temp local linux first so i can see what makes sense before we build someting that is not practical (possibly a new branch we can undo if it does not work)."

- Branch `theme-prototype`. Settings → Theme…: a colour per area (the tabs and the transport), with suggestions, Default, All default and "Tab headers in their page's colour"; saved in `[theme]` (colours quoted: `#` starts a comment in config.ini).
- Contrast: `areaPalette()` tints the controls from the colour (dark text on a light colour, light text on a dark one) and enforces every pair; a mid colour (mid grey, pure red) is moved towards white / black until all pairs reach the minimum, also trying stricter enforcement first (at AA the plain enforcement sometimes fixed one pair at the cost of another). State cues take the area's Accent / Border.
- The local build needed glibc 2.43; the user's machine has older, so the prototype was built in an Ubuntu 24.04 container (`localhost/minidaw-u2404:1`), offline from the already fetched sources (the container could not verify GitHub's certificate). `./tmp/try-theme.sh` runs it on a copy of the user's config.
- User: a crash after setting each colour. Cause: the Theme window kept a pointer into the colour list, which setting or clearing a colour moved. Reproduced under Xvfb (exit 139), fixed, checked with several clicks.
- User: "works now, tab headers look good". Choices (user: "yes, go ahead with those defaults"): the status line gets its own colour too, dark colours stay among the suggestions, tab headers coloured by default, one set for all projects.
- Done as P35 (SPEC-UI-20, T-UI-36): the status line as sixth area, its "lowest pair" over every area; Terms "Theme"; Getting started tip (`docs/Getting-started.md` again); README "Colours per tab"; Spec v0.82; version 1.2.0.

## 23. P36 prototype: a cable view for beginners (2026-11-26)

User: "i am considering simplefying the initial setup for beginners by using a cable based view. example: We have one or more input for audio, we have one or more inputs for midi, so we start by showing the socket and allowing the user to name the instrument in front of the socket from their view. The input then goes to the next step of direct recording (audio) or we use the midi to a vst instrument and that produces output (audio) that we can record."

- P35's CI run on `theme-prototype` (45196076832) is green.
- The cable view maps onto the existing tracks (socket = `channels` / `midiPort`, name = track name, instrument = `instrumentClass` with its linked sound track), so it is a view, not a new model.
- User: "yes prototype on a new branch, keep greyed sockets".
- Branch `cables-prototype` (from `theme-prototype`): a **Connections** tab, first in the default order (added before Terms in older configs). Rows: socket → name → goes through (audio: straight on, with a stereo tick; MIDI: the instrument / MIDI output list, Edit) → recorded (Arm, Listen, meter, which tracks) → Unplug. A free socket: type a name and press Enter (or Plug in) to make an armed track. A track whose input is gone keeps its row, greyed, "(not plugged in)". Several tracks on one socket: a row each. `socketRows()` / `socketLabel()` in core, T-UI-37. Renaming a MIDI track renames its sound track while that has the default name.
- Checked under Xvfb (32-channel ALSA default device, Midi Through): plug in by name + Enter, stereo tick, MIDI rows. ctest 287/287.
- User: "show only first 8 sockets plus show all". Free audio sockets past In 8 are hidden; a button above the table, "Show all N inputs (M more free)" / "Show only In 1-8 (and those in use)". Sockets in use always show. `./tmp/try-cables.sh` runs the 24.04 container build on a copy of the user's config.
- User: "works now, i see the connections tab; yes a good start, we may have to look what or the recording tab could move to the connections tab or a hint in the connections tab (a kind of link) to jump to a different tab but the correct track so the user does not have to search."
- Each connected row has **Track details >**: it opens the Recording tab scrolled to that track, the row (and an instrument's sound track) marked for 4 s with the selection colour and "> N" before its number. Checked under Xvfb with 8 tracks.
- User: "yes move the midi channel and add the back link; and the blue background highlickt on the current track is great we could use that automatically when the mouse moves over any track to directly see thet is current".
- The MIDI channel list is on the Connections tab, next to the MIDI socket; the Recording tab shows it as text ("all ch" / "ch 3", tooltip: chosen on Connections).
- Each Recording row has a "<" button: back to its socket on Connections (a sound track: its MIDI track's socket), scrolled there and marked with "> " and the selection colour for 4 s.
- The row under the mouse gets the selection colour, on both tabs; on Recording a MIDI track and its sound track light up together. Checked under Xvfb.
- User: the "> " made the row jump when it went away; redundant with the blue, or else keep its room. Kept (SPEC-UI-07: not by colour alone), its room now always reserved on both tabs, so nothing moves. Checked under Xvfb: marked and unmarked rows line up.
- User: "connections has a bit of whitespace between the horizontals, we could also do that on the recording to give more rest, on separate (unrelated tracks) i would suggest 2 times that whitespace, and on midi>audio tracks that same space as on connections, that clearly indicates what belongs together (perhaps we could even add a space unit in the config settings and we use 1 unit or 2 units for space separation margins".
- `[ui] space_unit` (pixels, 0 to 16, default 4; also a slider in Settings → Theme…; T-UI-38). Recording: no lines between rows any more; one unit between a MIDI track and its sound track, two between the others. Connections: one unit between sockets, two before the MIDI section. Gaps are empty table rows in the window colour; hover and jump find their rows past them. Checked under Xvfb with two instrument tracks (the test synth). ctest 288/288.
- User: "probably we should leave the unplug button at the same place as the plug-in button". Unplug is now right of the name, where Plug in is on a free socket.
- User: "if we select Stero with: e.g 7 stereo with 8 show 7+8 then i can still select stero with 9 on track 8, i guess when you combine a sterio that elliminates other combining untill the track is isolated again". The stereo tick is now off-limits (greyed, tooltip says why) when the next input is used by another track, or when this input is already the right half of another track's pair; making a pair mono (or unplugging) frees it. `channelUser()` in core, in T-UI-37. Checked under Xvfb: In 7 and In 8 plugged, In 7's stereo tick greyed.
- User: "yes, same rule on the recording tab". One rule in core, `inputConflict()` (T-UI-37): a stereo pair shares no input with any other track; two mono tracks may share one. The Input list on Recording greys what is not free (tooltip: which track has it); the Connections stereo tick uses the same rule; Add audio track takes the first input not in a pair (none: refused, "every input is in a stereo pair"). Checked under Xvfb: with In 1+2 on track 1, Add audio track took In 3 and its list greys In 1 and In 2.

User: "pushed, check the CI run; and going home" / "ok continue": run 45852261328 (ec847fc) green on all jobs.

User: "yes, add the terms, tutorial and readme; did we do a crosscheck before ?"
- No crosscheck since #28 (P34e, before 1.1.0): P35 and P36 are not crosschecked yet; #29 proposed for both.
- Terms: "Connections", "Socket", "Stereo pair" (new); Theme mentions Space between rows; Audio / MIDI track point to the Connections tab.
- Getting started: lesson 1 makes the audio track by naming a socket on Connections; lesson 2 the MIDI track by a MIDI socket and its instrument; Edit there too. `docs/Getting-started.md` regenerated.
- README: section "Connections", the tab in the summary and in "Colours per tab", Space between rows, the MIDI channel on Connections.

## 24. Crosscheck #29: P35 and P36 (2026-11-26)

User: "yes commit, then do the crosscheck" (5dd7650); meanwhile "ok add in memory, before release always crosschek" (kept as a rule: a crosscheck of every step since the last one before any release).

Three independent reviews (P35 code, P36 code, docs against code). Fixed:
- CC1 Tab headers in an area colour: the hover / press frame and the unfocused overline used the main Accent on the area's tab colours (about 1.2:1 on a dark area); now the area's own Accent.
- CC2 A colour picked in the Theme window's picker popup was saved only at exit; now saved when the mouse is released.
- CC3 The status line's "could not reach the minimum" warning counted only the main colours; now every area's too.
- CC4 Two MIDI devices of one name: their free sockets shared one ID and one name field; now told apart by their place (T-UI-37).
- CC5 Add audio track took In 1 even when a mono track had it; now the first input no track uses, else one a mono track has.
- CC6 A track from an older project overlapping a stereo pair: its own Input entry was greyed; now the current entry stays choosable, and Connections says "(shares an input with track N)".
- CC7 A track without input said "(not on this device)"; now "(no input chosen)". A pair that is not side by side names its real second input.
- CC8 The stereo tick: a refused click is logged, the tooltip also says "Not while recording".
- CC9 The gap rows made every row the same stripe; stripes now per track / socket.
- CC10 The Connections header row scrolled away; the table has its own vertical scroll now, header frozen.
- CC11 Docs: tab list and fallback in config warnings and code comments (from `defaultTabOrder()`); a config without `last` starts on Connections, like the default file; Connections says "Monitor" (not "Listen"), Terms Arm / Monitor / Track / MIDI track name the Connections tab; lesson 1 says In 1+2 has a track already at the first start; Spec SPEC-UI-11, -12, -09a (Theme window), SPEC-REC-01a (channel), status v0.83; misplaced comment.
Checked and not taken over: "the Connections table cannot scroll" and "the jump back does not scroll" (both wrong: checked under Xvfb with 32 inputs, Show all, a track on In 30 and "<" from Recording: scrolled and marked); old configs get Connections before Terms, not first (kept: their users know the program); the swatches being colour only (they are suggestions with the hex as tooltip, not a state); All default without confirmation (kept). README status 1.1.0 → at the release.
ctest 288/288.

User: "pushed, check the CI run" (run 45873727653, c1981d8): all green.

## 25. Release 1.2.0 (2026-11-26)

User: "yes, prepare 1.2.0".
- Crosscheck before the release: #29 done (section 24).
- CHANGELOG `## 1.2.0 — 2026-11-26` (Connections, Look); `tools/release_notes.py v1.2.0` reads it. README status 1.2.0 (steps P2–P36). Plan P35, P36 done. CMake already 1.2.0.
- To do: commit, merge `cables-prototype` (with `theme-prototype`) into `main` (fast-forward), push; CI; tag `v1.2.0`; `dist/` from the green main run.
