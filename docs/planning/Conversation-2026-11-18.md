# Conversation log — 2026-11-18

## 1. P28a: version shown and raised

User: "the mini-daw shows no version and that gets very confusing, can you show a version in either a about menue item or next to the name and increment it, now it shows constantly 0.1.0".

- Up to now the version was 0.1.0 from `CMakeLists.txt`, never raised. It was shown in the window title, the log and `--version`.
- Version rule (SPEC-UI-18, Claude's proposal, in CLAUDE.md):
  - the number is `0.<step>.<round>`, so now 0.28.1;
  - the crosscheck #23 fixes will be P28b, 0.28.2.
- The build information is written at build time by `cmake/build_info.cmake`: the commit, its date, whether there were local changes, and the GitHub run ID.
  - It is shown in Help → About mini-daw… (with Copy) and at the right end of the menu bar, where a click opens About.
  - It is also in the window title, the log, `--version`, the script report and the Windows file version (0.28.1.<run number>).
- Linux 244/244 and Wine 227/227. Checked in the GUI on Xvfb.

## 2. CI run 44339405990 (P28a)

User: "pushed it, check the CI run". Then: "yes add the retry; reran it, check the CI run; The mini synth has a installer on windows, we currently dont, can we add one ?"

- Linux 244/244, and the build calls itself "0.28.1 (commit 9f2165c of 2026-11-18, CI run 44339405990)". Licence check green.
- The Windows job failed at "Install NSIS": the Chocolatey feed returned 504 Gateway Timeout. Claude could not re-run the job (the token can only read Actions: HTTP 403).
- The workflow now tries `choco install nsis` up to 3 times, 60 s apart.
- Installer: mini-daw has had an NSIS installer since P9b (`mini-daw-<version>-setup.exe`, in `dist/windows`). Claude asked the user what the synth's installer does that this one does not.

## 3. CI run 44349222471 (P28a + NSIS retry)

User: "pushed it, check the CI run". The user's re-run of 44339405990 had not started; the new run covers the same code.

- windows, linux, licence green; Linux 244/244, Windows 230/230; both "0.28.1 (commit a1ca7b2 …, CI run 44349222471)"; file version 0.28.1.96; `mini-daw-0.28.1-setup.exe`.
- Installer: user: "Nothing, I missed it". `dist/TESTS.txt` now names the setup first, with T-PKG-03 (install) and T-UI-32 (version / About).

## 4. P28b: crosscheck #23 fixes (version 0.28.2)

User: "yes fix CC1–CC11, CC1 first".

- CC1: the export writes `timeUnit="seconds"` on Markers. The import reads Markers in the Arrangement's unit. Files from mini-daw before 0.28.2 are read as they were meant.
- CC2–CC9:
  - files are closed before a failed import is removed;
  - limits on clip places and lengths and on one track's WAV;
  - external files only from the file's folder;
  - lanes with the same name are numbered;
  - lines for cross-fades, clipping, submix / VCA tracks, empty lanes and cut loops;
  - a note still sounding at a clip's start plays from there;
  - the fade-out ends at the clip's end.
  - Found along the way (CC9): `takes` was checked after it had been made, so a failed import left the new project folder behind.
- CC4 / CC10: the folder is made absolute. The window says "Choose a folder outside the open project …". The "Auto record" track is counted in the export's track count. The Spec, Plan and Tests texts are updated.
- CC11: tests for all of these, plus WAV formats, the Browse… file list and a write failure (a test switch in the import).
- Linux 249/249, Wine 232/232.
- Also checked (new global rule): the repo has no releases, so the artifact cleanup does not apply (178 artifacts, 42 older than 14 days, 43 MB).
