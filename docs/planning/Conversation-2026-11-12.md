# Conversation log 2026-11-12

Continues `Conversation-2026-11-10.md` (crosscheck #18 fixes, LMW-Synth test, plugin formats, P25a script files, crosscheck #19).

## 1. CI of `a6c9316`; the log of the Windows problem

User: "pushed it, check the CI run" (the watcher of the previous session had stopped), then sent the old log of 2026-11-10 22:13–22:24.

- Run 42916639384: windows, linux, licence green; Linux 218/218 (all six script cases), Windows 209/209. Builds in `dist/`; `dist/TESTS.txt` has T-RUN-05; `instrument-switch.txt` (now with DX7 V, CS-80 V3, CS-80 V4 from the user's logs) copied to `dist/windows/`.
- The log: CS-80 V4 → CS-80 V3 on track 3 (1.6 s), its editor, restart requests (flags 0x4 = parameter values changed, harmless), CS-80 V4 on track 4, two auto record parts, a recording of 54.6 s, a normal close. No warning, no error, no refused action, and no switch to a MIDI output. Finding: choosing a MIDI output, a refused click (e.g. the X disabled while playing) and an instrument choice that does nothing leave no line in the log, so even the right log may not show the problem.

## 2. Log lines for track actions and disabled clicks (P25d)

User: "pushed it, yes add the log lines".

- `logRefusedClick` (a click on a disabled control logs why): X (remove; its tooltip also names the reason), Add audio / MIDI track, Arm, Edit, Render.
- Logged: track added / removed, armed / disarmed, instrument chosen (before loading; also when its track is gone), MIDI output chosen (and the instrument it replaced).
- Spec v0.54 (SPEC-LOG-01); T-LOG-06; Plan P25d. Checked: script reports and a click on the disabled Add audio track on Xvfb.

## 3. Crosscheck #20

User: "pushed it, check the CI run" and "and do the crosscheck". Crosscheck #20 (appended to `Crosscheck-2026-11-10.md`): reviewer subagent on P25d and the instrument-switch script, Claude on the docs. No high or medium findings: BB1 the output line also when nothing changes, BB2 / BB3 two silent track changes (render that cannot start, a MIDI track's sound track left as a normal track), BB4 docs.
- CI run 43120319952 (`2504b04`): windows, linux, licence green; Linux 218/218, Windows 209/209. Builds in `dist/`; `dist/TESTS.txt` has T-LOG-06 at the top.
