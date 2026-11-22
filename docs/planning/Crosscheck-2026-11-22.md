# Crosscheck #28 — 2026-11-22 (after P34a–d: macOS)

Scope: Spec v0.80 (SPEC-TECH-07), Plan (P34), Tests (T-MAC-01..08), README, Terms, Getting started, and the code at `00b6e58`.

How it was done:
- Claude checked Spec ↔ Plan ↔ Tests ↔ docs.
- An independent reviewer read the code (`git diff cd917dc 00b6e58`). It was a subagent given the code and SPEC-TECH-07, but not Claude's conclusions.
- Claude then checked CC2 and CC7 in the code.

Nothing could be run on a Mac: the macOS parts are only built and tested in CI. The last run was 45028873440, all green.

| Area | Verdict |
|------|---------|
| Spec ↔ Plan ↔ Tests | ✅ these are in the code and in CI: CoreAudio / CoreMIDI, the folders in `~/Library/Application Support` and `~/Music`, the VST3 folders and bundles, the NSView editor, the microphone permission, the unsigned universal `.app` in a `.dmg`, and the release taking the `.dmg`.<br>⚠️ the system folder dialog (NSOpenPanel) is not there **CC4**.<br>⚠️ presets use the Linux folders **CC3**.<br>⚠️ the Spec and Plan status lines still say "planned" **CC11** |
| Numbers as text | ✅ the standard functions on Linux / Windows (MinGW GCC 13 has them too); the fallback matches `to_chars` for finite values; "inf" / "nan" are now refused early, with the same message.<br>⚠️ denormal numbers on Apple's library **CC6** |
| Run loop | ✅ one `pollfd` per descriptor, every reader called, removal by an earlier callback handled, POLLNVAL as before |
| Microphone permission | ✅ the ask / answer order, one reopen per answer, retried after a recording, one question at a time, the test program never asks, a block with no captures.<br>⚠️ the reopen (and Apply) during a Render or an export **CC2**.<br>⚠️ the command-line tests on a fresh Mac **CC5** |
| Editor window | ✅ the order close → detach → destroy, the error paths, ARC bridging, sizes in points.<br>⚠️ the test synth's view without `removed()` **CC9** |
| Cmd instead of Ctrl | ✅ temporaries used safely, ImGui's Ctrl = Cmd on macOS.<br>⚠️ the Terms search **CC7**; the script help **CC8** |
| Build / CI | ✅ the release `if:` / `needs`, the `.dmg` loop with or without a `.dmg`, `release_notes.py --macos`, the Info.plist, the icon, `-sectcreate`.<br>⚠️ no time limit on the macOS job, which the release now waits for **CC1** |
| Sign-conversion clean-ups | ✅ no change in behaviour (indices are never negative) |
| Docs | ⚠️ Terms and Getting started name only Windows / Linux folders **CC11**.<br>⚠️ the microphone question comes again after an update **CC10** |

## Findings

| # | Severity | Finding | Suggested fix |
|---|----------|---------|---------------|
| CC1 | medium | The `macos` job has no `timeout-minutes`, and the release job now `needs` it. A hung test, a hung `hdiutil` (known to fail now and then with "Resource busy") or a long macOS queue holds up a tag's release for up to 6 hours. | `timeout-minutes: 60` on the `macos` job. The release's `if:` already copes with any result. |
| CC2 | medium | The reopen after macOS's microphone answer calls `applyAudioSettings` directly. It checks only `recording()`, not a Render (`renderTrack`), an audio export or other slow work in progress. Settings → Audio devices → Apply has the same gap; that one existed before P34. `applyAudioSettings` stops playback, reopens the stream and reloads the instruments in the middle of that work. | In `applyAudioSettings`: refuse with a clear line while a Render or an audio export runs, as for recording. The microphone reopen waits until both are over, keeping `micAnswersSeen` so it tries again. |
| CC3 | medium | `presetFolders` on macOS uses the Linux places (`~/.vst3/presets`, `/usr/share/vst3/presets`). VST3's places on macOS are `~/Library/Audio/Presets/<vendor>/<plugin>` (user) and `/Library/Audio/Presets/…` (all users). Presets saved by other programs would not be listed, and ours would not be seen by them. | An `__APPLE__` branch with those folders. Extend the preset-folder test. |
| CC4 | medium (Spec) | SPEC-TECH-07 says the folder choice uses the system dialog (NSOpenPanel). On macOS, `FolderChooser` uses the built-in browser, as on Linux. It works, but it is not what the Spec says. | Either NSOpenPanel as a sheet on the main window (`beginSheetModalForWindow`; GLFW's event polling runs its completion; files with an extension for Import DAWproject), or change the Spec to "the built-in browser, as on Linux, while macOS is a preview". |
| CC5 | low | `--audio-test` / `--record-test` on a Mac where the microphone was never allowed start macOS's question, but nothing waits for the answer, so the test records silence. Only a line in the warnings shows it. | In those two modes, when an input is opened and the answer is still open: wait up to 30 s for it (`micAccessAnswers`), then reopen. Refused → exit code 1 with the line. |
| CC6 | low | `parseDoubleFallback` reads through a stream. libc++ fails the stream when `strtod` reports ERANGE, which Apple's `strtod` may do for denormal numbers (e.g. `5e-324`). `from_chars` returns them. The format fallback then writes 17 digits instead of the shortest. Not a practical value in mini-daw. | `strtod_l` with the C locale after the grammar check. ERANGE counts only when the result is 0 or infinite. |
| CC7 | low | Terms on macOS show Cmd+…, but the search looks in the Ctrl text: "Cmd+R" finds nothing. | `findTerms` also looks in `keysHere(text / where)`. |
| CC8 | low | The script command list (`--script` help, remote control) says "Record, Ctrl+R" on macOS too. | `keysHere` where the list is written. |
| CC9 | low (test only) | The test synth's `~TestSynthView` does not release `nsview_`, so a host that drops the view without `removed()` leaves it with a click pointer to freed memory. A second `attached()` leaks the first view. | Remove the view in the destructor and before making a new one. |
| CC10 | doc | Each build has a new ad-hoc signature, so macOS probably asks about the microphone again after every update (the grant belongs to the signature). | A line in README → Installing → macOS. |
| CC11 | doc | Texts not yet updated for the Mac:<br>• Terms "Default folder" names only Windows and Linux.<br>• Getting started lesson 2 names only the Windows / Linux VST3 folders.<br>• Spec "Platforms" and the Spec status still say macOS planned.<br>• Plan row P34 says "planned". | Add the macOS folders (texts, then `docs/Getting-started.md` again), and update the Spec status and the P34 row. |
| CC12 | note | The build for both architectures turns off Opus's SIMD code for arm64 too, so Opus export is slower on Apple silicon (correct, only slower). | Accept for the preview. Later, build each architecture apart and join them with `lipo`. |

Test coverage the reviewer misses:
- the microphone flow (reopen on the answer, waiting during a recording);
- the fallback compared on macOS (denormals, hex, long text);
- the window-level editor code (child window, snap-back, error paths);
- the release packaging, which only a tag shows (a `-rc` tag would).

## Actions taken

The user chose to fix CC1–CC11, with CC4 done as NSOpenPanel. CC12 is accepted for the preview, and the release keeps not waiting for the macOS job.

| # | Done |
|---|------|
| CC1 | `timeout-minutes: 60` on the `macos` job |
| CC2 | `audioReopenBlocker()`: Apply and the reopen after the microphone answer are refused, with a clear line, while a recording, a Render or an audio export runs. The reopen also waits for slow work (`busy`) and tries again afterwards |
| CC3 | macOS preset folders: `~/Library/Audio/Presets/<vendor>/<plugin>` (user), `/Library/Audio/Presets/…` and `/Network/Library/Audio/Presets/…`. T-VST-35 checks the user and all-users folders per system |
| CC4 | `folder_chooser_mac.mm`: NSOpenPanel as a sheet on the main window (folders, or files of one type via `UTType`); the answer comes into a shared object and is taken over in `update()`, like the Windows thread's. Cancel and the destructor close it as its Cancel button does. If it cannot be shown, the built-in browser is used, with a note |
| CC5 | `--audio-test` / `--record-test`: while macOS's question is open, they wait for the answer for up to 30 s and open the devices again. Refused, restricted or still not answered → the line and exit code 1 |
| CC6 | The parse fallback uses `strtod_l` / `_strtod_l` in the "C" locale. Out of range only when the result is infinite, or 0 from non-zero digits. T-MAC-05 has denormal, too-small and `0e-999` cases (the fallback and the standard agree on Linux) |
| CC7 | `findTerms` searches the text as shown (`keysHere`) |
| CC8 | The `--help` command list and `GET /api/commands` give the text through `keysHere` |
| CC9 | The test synth removes its NSView in its destructor and before a second one; the view's click pointer is cleared when it goes |
| CC10 | README → Installing → macOS: macOS may ask about the microphone again after each update |
| CC11 | • Terms "Recording folder": Linux and macOS.<br>• Getting started lesson 2 tip names `/Library/Audio/Plug-Ins/VST3` (and `docs/Getting-started.md` again).<br>• Spec v0.81: status, Platforms, SPEC-TECH-07 with the preset folders and Cmd.<br>• Plan rows P34 and P34e updated |
