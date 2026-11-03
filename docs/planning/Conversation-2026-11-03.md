# Conversation log — 2026-11-03

Continues `Conversation-2026-11-02.md` (P7, P7b, P8, P9 and crosschecks #5 and #6 were done there).

## 1. CI after P9
> pushed

- Run 41540018781 (`6536815`, P9): every step of the linux, windows and licence jobs passed via the API; nlohmann/json fetched and licence-checked. The job logs could not be downloaded here (`http2: frame too large`), so the test count and the MSVC warning count are not confirmed this time.

## 2. Crosscheck #7
> do the crosscheck #7

- Written up in `Crosscheck-2026-11-02.md` (Claude + independent reviewer; N1 and N2 reproduced with a small program).
- Two high findings: N1 a wrong-typed `format`/`version` in project.json throws (a crash at every start, since the last project is reopened); N2 a non-UTF-8 name makes autosave throw. Medium: N3 name collisions when copying takes in, N4 a failed copy still switches the project, N5 open failures not shown, N6 repairs not saved (skipped tracks lost on the next save). Low: N7–N9; docs N10; tests N11; notes N12.
- Waiting for the user's go.

## 3. Crosscheck #7 fixes
> yes fix N1–N11, N1 and N2 first

- N1: type checks + catch-all around project parsing; N2: UTF-8-safe saving. Then N3–N11 (see `Crosscheck-2026-11-02.md`).
- `importTakes` now returns copied / missing / failed; collisions get `-2`, `-3`; Save as only switches when all copies worked.
- Repaired projects keep `project.json.bak`; open failures shown in the status line; Recent marks missing projects.
- Spec v0.21; tests T-PRJ-07, T-PRJ-08, T-PRJ-03/04 extended. Linux 119/119, Wine 118/118; N1/N5/N6 checked live.
- A safety check refused my `rm -f $O/*` (would be `/*` if `$O` were empty); rerun with `"${O:?}"/*`.

## 4. CI, Linux build behind TLS inspection
> pushed it, check the CI run
- Run 41602682487 (`f863bda`): build, test and smoke steps passed on Windows and Linux, licence passed (read via the API).
- The container build failed: "server certificate verification failed". The office network inspects HTTPS ("Reversinglabs D.o.o. Forward Trust CA"); the host trusts that CA, the container did not. Fixed by mounting the host's CA bundle read-only into the container (documented in `CLAUDE.md`). 119/119, GLIBC ≤ 2.38; `dist/` refreshed.

## 5. P9b: Windows packaging
> start P9b

- User's choices: mini-daw.exe (no console) + mini-daw.com (console) — the Visual Studio `devenv.com` trick; NSIS; current user, no admin; Claude draws the icon.
- Icon: `tools/icon/make_icon.py` (Pillow) → `res/mini-daw.ico` (7 thicker bars at ≤ 32 px), PNG, `icon_data.hpp` for the Linux window.
- CMake: app code as an OBJECT library linked into `minidaw` (WIN32) and `minidaw_cli` (`.com`); resource file with `GLFW_ICON` and VERSIONINFO; `installer` target (makensis). `mini-daw.exe` given an option attaches to the parent console.
- `tools/check_pe.py` (subsystem, icon, version info) as ctest `pe_check` on Windows; CI: NSIS install if missing, PE check, smoke-run via `.com`, installer built and uploaded.
- Tested: PE check; `.com` exit codes under Wine; installer built with makensis in a container, silent install/uninstall under Wine (config kept); Linux window icon via xprop. The installer pages could not be seen under Wine on Xvfb: for the user on Windows.
- Spec v0.22 (PKG-01..03), tests T-PKG-01..04, THIRD_PARTY.md "Build tools" (NSIS, Pillow).

## 6. CI after P9b and crosscheck #8
> pushed it, check the CI run
> do the crosscheck #8

- Run 41606309851 (`497c975`): MSVC builds both programs, tests, PE check and `.com` smoke run pass; the installer step fails (`File … -> no files found`). Log fetched with `curl --http1.1` and the gh token (gh itself fails on this network).
- Cause (O0): Windows makensis splits `File` paths at the last backslash; my paths used `/` only. The container test (Linux makensis) passed, and the CMake `installer` target had never run on Windows: my test gap.
- Crosscheck #8 in `Crosscheck-2026-11-02.md`: O0 high (CI), O1 artifact layout, O2 NSIS unpinned, O3 .ilk in Debug, O4 install while running, O5 older over newer, O6 console attach, O7 text size on high-DPI displays (GLFW already makes the process DPI aware), O8 docs. Waiting for the user's go.

## 7. Crosscheck #8 fixes
> yes fix O0–O8, O0 first

- O0: platform separator in the NSIS script + native paths. Wanted to prove it with the Windows makensis under Wine: the download (NSIS 3.10 zip from SourceForge, in scratchpad/nsis-dl) was allowed, running it was refused (code from an outside source) — not worked around; CI will confirm.
- O1 flat download folder, O2 NSIS pinned 3.13.0, O3 `/INCREMENTAL:NO` for the .com, O4 install/uninstall wait for mini-daw to close, O5 older-over-newer question and old folder removed, O6 narrower console attach, O7 display scaling (text, spacing, window size), O8 docs.
- A safety check refused `rm -f "$ST"/*setup*.exe` (unguarded variable) — rerun as `"${ST:?}"`.
- Tested: installer rules under Wine (older refused, folder move, while running: exit 2), 150 % scaling on Linux (needed `Xvfb -noreset` and `xrdb -nocpp`).

## 8. CI after crosscheck #8
> pushed it, check the CI run
- Run 41609704861 (`a5c68a8`): green. The installer is now built by the Windows makensis (O0 confirmed); NSIS 3.13.0 pinned; the Windows download is flat (setup 0.67 MB, exe/com 1.4 MB each from MSVC); PE check on the downloaded files OK. Artifact fetched with curl over HTTP/1.1.
- Not committed: `res/*.xmp` (darktable sidecars of the user's images).

## 9. P10: VST3 instruments (a–c, e)
> start P10

- User's choices: in-process hosting; scan on demand with a cache; editor on Windows first; test with Surge XT, Dexed and a test synth; later Cherry Audio and Arturia on Windows 10.
- SDK v3.8.1_build_84 (2026-08-11, MIT) as a dependency, only `base`, `pluginterfaces`, `public.sdk`; the hosting sources compiled as `minidaw_vst3host` (Linux and MinGW).
- P10c test synth first (separate processor and controller, gain parameter, state), as a real bundle for the tests.
- P10a scanning (`plugin_scan`), P10b `Vst3Instrument` + `InstrumentRack` (hazard-protected config, live MIDI taps on MidiInputs, playback MIDI at the sample, notes released at Stop), GUI (Settings → Instruments, instrument per MIDI track), P10e state in project.json (base64).
- Read the SDK's VST Usage Guidelines: "VST®" on first use + attribution in docs; the "VST Compatible" logo question is open for the user (SPEC-LIC-05).
- Found on the way: missing SDK sources (pluginview, thread checker), negative file-clock values in `moduleStamp`, my test IDs clashing with the existing T-VST-01..07 (renumbered), and my own mis-clicks in the GUI test (redone).
- Tests: Linux 127/127, Wine 126/126; live GUI test with the test synth (scan, live play, record, playback, project restore).
- Open: P10d (editor), real plugins (T-VST-17), the logo decision.

> yes commit it, i'll push; text only, no logo
- SPEC-LIC-05: no VST Compatible logo; text form ("VST®", attribution sentence) only.

## 10. CI after P10
> pushed
- Run 41629642662 (`7db2936`): linux and licence pass; windows fails to compile the SDK's `module_win32.cpp` with MSVC: the SDK's `SMTG_CPP20` uses `__cplusplus` only, which MSVC reports as 199711 unless `/Zc:__cplusplus` is given. Added for the SDK host library (public) and the test synth. Could only be checked by CI (no MSVC here).

## 11. CI after the MSVC fix: Windows tests hang
> pushed (run 41632628383)
- `a57e8cf`: MSVC now builds everything; linux and licence pass. The Windows Test step hung for over 54 minutes.
- `gh run cancel` was refused (HTTP 403: the personal access token may not cancel runs); the user cancelled the run in the web UI.
> stuck at: … Start 105: T-VST-10: moduleinfo.json is used without loading; damaged modules fail cleanly
- Cause (most likely): T-VST-10 loads a text file as a plugin. The SDK passes it straight to `LoadLibraryW`, and on real Windows that can open a system error dialog ("Bad Image" / "not a valid Win32 application") that nobody can close on the runner. Wine shows no such dialog, so the local tests passed.
> yes cancel it and do both fixes
- Fix 1: `NoErrorDialogs` (`SetThreadErrorMode` with FAILCRITICALERRORS, NOGPFAULTERRORBOX, NOOPENFILEERRORBOX) around loading a module in the scanner and around loading and unloading an instrument; the old mode is restored afterwards. A broken plugin now fails with an error message, also in the program itself, where the dialog would otherwise interrupt a scan.
- Fix 2: `ctest --timeout 300` in both CI test steps, so that a hang fails within minutes and shows which test hung.
- Linux 127/127, Wine 126/126. Only CI can confirm the fix (needs real Windows).

## 12. CI after the error-dialog fix
> pushed it, check the CI run
- Run 41703163695 (`fcf1e86`): green on windows, linux and licence. Windows: 128/128 tests in 30 s; T-VST-10 now passes in 0.02 s (it hung before the fix, which confirms the error-dialog cause). PE check, `.com` smoke run and the installer all pass. This is the first fully green Windows run with VST3 hosting (P10a–c, e).

## 13. Windows test of P10 by the user
> ok tested it on windows, i can hear the audio in (still with addtional pulsing nose but i will check for interference), the vst3 instruments audio is on the speaker if i arm the track, the vu level bars are ok but to the right you show numbers that vary in required space … best allocate a fixed space for the max number … the gui is some times a bit sluggish becaude of lacking visual feedback … Arturia instruments have many diffent pre-sets … a instrument pup up when pressing a button for that track (like bandlab sonar) … What do you thingk ?
- Real VST3 instruments (Arturia) work on Windows 10: found, heard live. Pulsing noise on the input: the user checks for interference first.
- Fixed now: the dBFS text and the CLIP button get a fixed width (SPEC-MON-02a, T-MON-07); built on Linux and MinGW, not yet seen on screen with a live signal.
- The instrument pop-up is exactly P10d (plugin editor); presets in Arturia live in its own window. Added later items: PL1 look and feel (hover/selection feedback), PL2 a preset menu from VST3 program lists.
