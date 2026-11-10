# Crosscheck #19 — 2026-11-10 (after P24a and P25a: crosscheck #18 fixes, script files)

Scope: Spec v0.52, Plan, Tests, README, CLAUDE.md, code at `c7a0e7a` (changes since `0cc45e6`: P24a `acb4271`, P25a `c7a0e7a`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Claude checked at the code: AA1 (`scriptState(app).dump(2)` and `value->dump()` in `compareStateValue` use nlohmann's strict UTF-8 handler, while `projectToJson` already uses `error_handler_t::replace` since crosscheck #7 N2; nothing in `stepScript` catches), AA2 (the `Busy` check for `instrument` tests "loaded and a class" only; `requestInstrument`'s work returns early while recording / rendering and leaves the old instrument), AA3 (`returnToStart` returns silently while recording; `saveProjectNow` returns void), AA4 (`lastAdded` is set only by add-track).

| Area | Verdict |
|------|---------|
| P24a run loop / frame / X11 / Terms | ✅ entries moved out before they are destroyed, a closed descriptor dropped without a call and logged, records by kind, X11 platform and display checked, Terms mark and view as specified |
| Script parser (core) | ✅ quotes, `\"` / `\\`, other backslashes kept, `#` only at a word's start, UTF-8 BOM, CRLF, last line without a line end, error line numbers; operator position consistent with the runner; ⚠️ argument words partly case-sensitive **AA5**; a text "true" / "null" cannot be expected **AA6**; a quote inside a word, a UTF-16 file **AA7** |
| Script runner (GUI) | ✅ one line per frame after the busy work, `current` points into a list that never changes, the window not touched after it is gone, exit codes 0 / 1 / 2 / 3, report as specified; ❌ a name that is not UTF-8 in the state throws out of the frame loop **AA1**; ⚠️ `instrument` can say ok for the old instrument **AA2**; `home` / `save` always ok **AA3**; `last` stale after remove-track / open **AA4**; skipped lines not listed **AA8**; the exit-3 path rewrites a report that may belong to the running script, and early errors only on stderr **AA9** |
| Shared track actions | ✅ the same behaviour as the old inline button code (compared with `0cc45e6`) |
| Tests / CI | ✅ T-RUN-03 / -04 test what they claim, own work / config / lock folder each, no flakiness seen; ⚠️ no automatic test of exit 3, closing early, `stay`, `quit`, `stop-on-failure on`; the script tests run on Linux only (Windows by hand under Wine) **AA10** |
| Spec ↔ Plan ↔ docs | ⚠️ SPEC-RUN-01 lists the command-line runs that are not blocked but not `--script` (exit 3, no show request); SPEC-RUN-02 "`#` starts a comment" (only at a word's start); Plan PX row **AA11** |

## Findings

| # | Sev | Finding | Proposed fix |
|---|-----|---------|--------------|
| AA1 | medium | `state`, `expect` and `wait-for` turn mini-daw's state into JSON text with nlohmann's strict handler, which throws on text that is not valid UTF-8 (a device, port, track or file name with odd bytes; on Windows a driver name in the ANSI code page — whether RtAudio / RtMidi can give one is a guess). Nothing catches it in `stepScript`: mini-daw ends with an exception and the report has no Result line. | Dump with `error_handler_t::replace` (as `projectToJson` does) in both places; test with a non-UTF-8 name. |
| AA2 | medium | `instrument` waits for the busy work and then only checks that the track has some instrument loaded. If the work did not change it (recording or a render started meanwhile, the track gone), the old instrument is reported as "loaded <old name>", so a test can pass while nothing happened. | Check that the loaded class is the one asked for (and the work's own result flag, as `open` has). |
| AA3 | low | `home` is always ok, also when `returnToStart` does nothing (during a recording); `save` is always ok, also when writing fails. | `home`: fail while recording (as the GUI ignores it); `save`: a result from `saveProjectNow` (error text), fail with it. |
| AA4 | low | `last` keeps the number of the track the last add-track made, also after that track is removed or another project is opened; a later track with that number would be taken. | Clear it on `remove-track` of that track and on `open`. |
| AA5 | low | `add-track MIDI`, `arm 1 ON`, `stop-on-failure ON` are errors, while `instrument` names and `output none` take any case. | Lower-case those fixed words in the parser. |
| AA6 | low | `expect <path> true` / `null` / `12` always mean the bool / null / number; a text "true" or "12" (a preset or track name) cannot be expected. | Keep which words were quoted: a quoted word is always text. |
| AA7 | low | `"abc"def` is two words, `a"b c"` is `a"b` and `c"`; a file saved as UTF-16 ("Unicode" in Notepad) gives a puzzling "unknown command" on line 1. | A quote inside a word is an error; a UTF-16 byte order mark gives "save the script as UTF-8". |
| AA8 | low | With `stop-on-failure on` the lines after a failure are left out without a word; "Result: N command(s)" counts only those that ran. | "Skipped K line(s) after the failure" in the report. |
| AA9 | low | The exit-3 path (mini-daw already runs with that config) opens and empties the report first, which may be the running script's own report. Errors before the window (script errors, unreadable script, report not writable) go to stderr only: `mini-daw.exe` started without a terminal shows nothing. (Script and report paths come from the narrow `argv`, as `--config` does: characters outside the Windows code page cannot be used — a known limit, not changed now.) | Open the report only after the lock is taken; write script errors into the report too when it can be opened. |
| AA10 | low | Test gaps: exit code 3, closing the window before the end, `stay`, `quit`, `stop-on-failure on`, a non-UTF-8 name (AA1), a last line without a line end and line numbers after comments in T-RUN-03; the script tests are skipped without `DISPLAY` and run on Linux only (Windows checked by hand under Wine). | Add `quit` / `stop-on-failure` / exit-3 / AA1–AA4 cases to the ctest scripts and T-RUN-03; Windows CI stays manual for now (a GUI on the Windows runner is untried). |
| AA11 | doc | SPEC-RUN-01 does not name `--script` (exit 3, the running one not brought up); SPEC-RUN-02 says "`#` starts a comment" without "at the start of a word"; Plan PX row. | Update the texts. |

CI run 42914083387 (`c7a0e7a`): windows and licence green, Windows 209/209; **Linux 213/215**: T-RUN-04 `basic` and `failures` failed, a defect of the test helper (no `cmake_minimum_required`, so CI's CMake 3.x applies the old CMP0053 rules and replaces `@WORK@` in the helper's own search text). Fixed with the findings below.

## Actions taken (user: "yes fix AA1–AA11, AA1 first")

| # | Action |
|---|--------|
| AA1 | `stateText()` (core) dumps with `error_handler_t::replace`; used by `state` and for `seen` in `compareStateValue`. Test with "Roland \xDCnit". |
| AA2 | The instrument work sets the script's result only when the class asked for is the one loaded; the line fails otherwise ("not loaded (see the log)"). (Not reachable in the ctest scripts: it needs recording or a render meanwhile.) |
| AA3 | `home` fails while recording (without auto record), as the window ignores it; `save` fails with `projectError`. |
| AA4 | `last` cleared when its track is removed and on `open`; tested in `failures`. |
| AA5 | The fixed words of add-track, arm and stop-on-failure are lower-cased by the parser, `last` and `none` compared in any case; tests. |
| AA6 | `ScriptLine::quoted`; a quoted value is always text in `expect` / `wait-for`; tests. |
| AA7 | A quote inside a word or text right after a closing quote is an error; a UTF-16 byte order mark gives "save it as UTF-8"; tests. |
| AA8 | "Skipped N line(s) after the failure (stop-on-failure on)"; tested in `stop`. |
| AA9 | The report is opened only after the lock is taken (exit 3 writes nothing); script errors are also written into the report with "Not run: the script has N error(s)". Narrow `argv` paths: unchanged (as `--config`). |
| AA10 | T-RUN-03 extended; new ctest cases `stop`, `quit`, `locked` (the lock held by `flock`, skipped if there is none); `run_script.cmake` gets `cmake_minimum_required`, NOT_IN texts and refuses empty texts. Closing the window early stays a manual check. |
| AA11 | SPEC-RUN-01 names `--script` (exit 3); SPEC-RUN-02 (comments, quotes, any case, UTF-16, the new rules); Plan P25c and the PX row. |
