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

## 5. CI run 44369824195 (P28b)

User: "pushed it, check the CI run".

- windows, linux, licence green; Linux 249/249, Windows 235/235. The write-failure case also passed on real Windows (CC2). Version 0.28.2, file version 0.28.2.97, `mini-daw-0.28.2-setup.exe`.
- `dist/` refilled. `dist/TESTS.txt` has the P28b steps: markers in another program, the open-project folder line, the Auto record count, odd files.

## 6. P25b: remote control over HTTP + JSON (version 0.28.3)

User: "pushed it; start P25b".

- How it works:
  - Switched on with `--remote` or `[remote] enabled`; port 7800.
  - `GET /api/state`, `GET /api/commands`, and `POST /api/command` with `{"line": …}`.
  - The answer comes when the command is done, with its note and the log lines it wrote.
  - One command at a time (409 otherwise); the state can be read meanwhile.
  - The commands run on the GUI thread through the script runner.
- Who may send:
  - this computer always;
  - the `[remote] allow` networks with a token, made by mini-daw and saved in the config;
  - never web pages: Origin refused, the Host must be an address, JSON only.
  - `export` only from this computer; `stay` / `stop-on-failure` never.
  - Commands from outside count as the user's own (the project is remembered).
- Found while testing: cpp-httplib's default socket options let a second program listen on the same port. The port is now exclusive (SO_REUSEADDR on Linux, SO_EXCLUSIVEADDRUSE on Windows).
- Version: P25b comes after P28, so it is 0.28.3 (rule: a step with a lower number done later raises the round).
- Terms: "Script file" and "Remote control" (50 terms).
- cpp-httplib v0.67.0 (released today, with security fixes), pinned by hash.
- Linux 254/254 (T-RUN-07: 29 checks over HTTP). Wine 236/236, and mini-daw.exe `--remote` under Wine answered curl.
- By hand: the token path through the VM's own address.
- Still to test: from another computer to the Windows machine (T-RUN-07 M).

## 7. CI run 44389452648 (P25b)

User: "pushed it, check the CI run".

- windows, linux, licence green; Linux 254/254 (T-RUN-07 over HTTP under `xvfb-run`), Windows 239/239. Version 0.28.3, file version 0.28.3.99, `mini-daw-0.28.3-setup.exe`.
- `dist/` refilled. `dist/TESTS.txt` has the P25b steps: T-RUN-07 M (commands from another computer to the Windows VM with the token, the Windows network question), the two new terms, version 0.28.3.
- README: the status line still said 0.28.1; now 0.28.3 (steps P2–P28b).

## 8. Crosscheck #24 (P25b)

User: "pushed it, do the crosscheck".

- `Crosscheck-2026-11-18.md`: an independent reviewer (subagent) read the code and cpp-httplib; Claude checked its points at the code and did the doc check. No bypass of the access check was found.
- Findings CC1–CC8: a token holder can still `import` / `open` / `record` and reach a `\\host` path on Windows (CC1, medium); a long `wait` cannot be ended from outside (CC2, medium); closing can wait 5–10 s for open connections (CC3); local programs need no token (CC4); two state reads at once give 409 (CC5); 413 and the Content-Type compare (CC6); request text in the log (CC7); doc mismatches and test gaps (CC8).

## 9. P25f: crosscheck #24 fixes (version 0.28.4); P29 planned

User: "yes fix CC1–CC8, CC1 first; add for the future: export-render a selection of takes or all takes as a music stream (ogg and other common formats) with a option to zip that with the midi file as final product."

- CC1: from other computers `import` is refused too, and so is any word that is a network path (`\\host`, `//host`, `\\?\UNC`, `\\.\`): Windows would log on to that host. Spec and README say what a token holder can still do (open projects, record, instruments).
- CC2: `POST /api/cancel` ends a running `wait` / `wait-for`; a client that goes away does the same (the server asks the socket a few times a second); `wait` at most 600 s over remote control.
- CC3: one request per connection, read timeout 2 s, write 5 s.
- CC4: README: "this computer" means every program on it; on Linux config.ini is made 0600 when it holds the token. Not done: a token also from this computer (not asked for).
- CC5: the state channel holds up to 16 requests; each frame answers all. CC6: 413 in the Spec, Content-Type compared in any case with parameters. CC7: control characters refused in a command line; refusal lines show them as `?`.
- CC8: Plan P25 row, the Terms test id, CLAUDE.md, Plan PX row; the T-RUN-07 M steps in `dist/TESTS.txt`.
- The channel between the server and the window was rewritten: a list of slots, each request waits for its own answer.
- Tests: Linux 254/254 (T-RUN-07 39 checks), Wine 236/236; by hand from the VM's own address and under Wine (cancel, a client that gave up).
- Planned: SPEC-PRJ-06 / P29 Export audio. Questions for the user before it starts: one file per take or one mix of the chosen takes; with the project mix or as stems; which formats (Ogg Vorbis, plus FLAC, WAV, MP3, Opus?) and quality; MP3 needs an encoder (LAME is LGPL); which MIDI goes into the zip.

## 10. CI run 44398240257 (P25f)

User: "pushed it, check the CI run".

- windows, linux, licence green; Linux 254/254 (T-RUN-07 39 checks), Windows 239/239. Version 0.28.4, file version 0.28.4.101, `mini-daw-0.28.4-setup.exe`.
- `dist/` refilled; `dist/TESTS.txt` at 0.28.4: T-RUN-07 M with the P25f steps, Terms, version.
