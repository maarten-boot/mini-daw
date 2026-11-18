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
