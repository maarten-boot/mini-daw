# Conversation log 2026-11-10

Continues `Conversation-2026-11-09.md` (P23 Terms tab, P24 Linux editor, crosscheck #18).

## 1. Crosscheck #18 fixes (P24a)

Claude: "Shall I fix Z1–Z9, starting with Z2?" User: "yes".

- Z2: the run loop and the editor's frame release handlers (and with them plugin objects) only after their lists are consistent, so a destructor that unregisters is safe.
- Z1: a reader whose descriptor was closed is dropped without a call; the app logs a warning.
- Z3: Edit on Linux only when GLFW runs on X11; an error instead of a crash otherwise.
- Z4: test synth "Editor test mode" (register in setFrame, close the pipe without unregistering, unregister in the destructor), tested in T-VST-38; Z1 / Z2 unit tests in T-VST-37.
- Z5: unregister matches reader / timer. Z6: T-VST-40 step for typing in a plugin's window.
- Z7 / Z8: Terms mark cleared by a new search; the result list for any word.
- Z9: Plan P10d, CLAUDE.md, Spec (v0.51) updated.
- Linux 209/209 under xvfb-run; run-loop tests clean under AddressSanitizer; MinGW under Wine 204/204.

## 2. CI of `acb4271`

User: "pushed it, check the CI run". Run 42881121369: windows, linux, licence green; Linux 209/209 under `xvfb-run` (T-VST-39 passed), Windows 206/206. Both builds checked (the new texts are in them), then `dist/` refilled; `dist/TESTS.txt` notes the crosscheck #18 fixes and the extra steps in T-VST-40 and T-UI-28.

## 3. Other plugin formats

User: "are different plugin formats common on Mac and Linux, should we (in the future) add other formats besides VST3?" Claude: AU is Apple's own (Logic / GarageBand load only AU; Apple's built-in instruments are AU only), CLAP is new and cross-platform but plugins ship it next to VST3, LV2 is Linux's open format (mostly effects; instruments also VST3 now), VST2 is no longer licensed, AAX is Pro Tools only. Advice: stay with VST3; AU with a Mac version; CLAP on request; a small format layer first. User: "yes add it to the plan" → Plan P12a (later).

## 4. T-UI-28 result; a real Linux plugin for tests

User: "T-UI-28: read the text as intro for new users it is a good start. For linux we now have a custom made instrument on this machine using VST3. see the project at ../software-synth-LMW/ perhaps usefull for testing stuff."

- T-UI-28 texts: pass (user).
- LMW-Synth (JUCE 9, VST 3.8, AGPLv3; the user's own project) copied to a scratch `~/.vst3` and tried in mini-daw (`acb4271`) on Xvfb: Edit opens its full JUCE editor; drag, typing (Z6), drop-down menus, Close, Edit again, quit with it open, saved state and restart all work (Tests.md T-VST-40 row). Left for the user: stacking / minimising with a window manager, and sound.

## 5. User's Windows test

User: "testes some on windows, Large arturia instruments show the waiting message, making notes on the recording while recording give no interruptions on audio. but i loaded 3 instruments from arturia and then the instrument could switch to the built in windows midi instrument but no longer back to any other. i could not even clode the track ao in the end i had to close the program"

- Claude's first reading of the code: changing the instruments waits (`InstrumentRack::configure`) until the audio thread has left the old list, without a time limit; if a plugin hangs in its processing, the GUI waits for ever. Not confirmed: the log of that run and whether the window froze were asked for.
- User sent part of a later log: the T-VST-36 lines (DX7 V: 128 programs in 'ProgramList Ch1'; DCO-106 (Cherry Audio): none). It does not cover the run with the problem.
- User: the window did not freeze (Task Manager: mini-daw at the top of the list, 300 MB, 4.5 % CPU); not recording; logs later, will retry a similar setup. So not the endless wait of `configure` (that freezes the window); the cause is still open.

## 6. Idea: remote control for automatic tests

User: "if we allow the mini-daw gui to be remotely controlled e.g. using 9p protocoll or zmq or https could we then do some of the tests automatically?" Claude's answer: see the reply; decision pending.
- Claude: a command layer (the GUI's actions, run at the start of a frame), a script file first (`--script`), then local HTTP + JSON (cpp-httplib); off by default; ImGui Test Engine mentioned (licence not plain open source). User: "yes add it to the plan, script file first; during development it could be very handy to also allow 192.168.1.0/24 my home network as my windows and synth is in the basement on that net while development happens on linux elsewhere in the house, it could be stripped down later" → Plan P25, P25a (script), P25b (HTTP, allowed networks incl. 192.168.1.0/24 during development, token for non-local clients).

## 7. P25a: script files

User: "pushed it, yes start P25a".

- Core `script` (`script.hpp/.cpp`): words with quotes and Windows paths, comments, the command list with usage texts, the whole file checked first, dotted state paths, `expect` comparisons. nlohmann_json is now a public dependency of the core.
- `main.cpp`: the track actions as functions shared by the buttons and the script (`addTrack`, `removeTrackNow` / `whyNoTrackRemove`, `setMidiOutput` / `dropEmptyLinkedTrack`, `requestInstrument`, `setArm`); `ScriptRun`, `scriptState` (JSON), `stepScript` (one line per frame after the busy work), report with the start-up log and the log per line; `--script`, `--script-report`, exit codes 0 / 1 / 2 / 3, commands in `--help`. `last` = the track the last add-track made.
- Tests: `test_script.cpp` (T-RUN-03); ctest runs mini-daw with scripts `basic`, `failures`, `bad` (T-RUN-04, Linux with a display; CI already runs under xvfb-run). Linux 215/215; MinGW under Wine 207/207, and `basic` with the Windows build under Wine (28 / 0 failed).
- `tests/scripts/manual/instrument-switch.txt`: the user's problem as a script (T-RUN-05).
- Spec v0.52 SPEC-RUN-02; Plan P25a done; README, CLAUDE.md.

## 8. CI of `c7a0e7a`, crosscheck #19, fixes

User: "pushed it, check the CI run" and "and do the crosscheck". Run 42914083387: Windows 209/209, licence green, Linux 213/215 — the script tests `basic` / `failures` failed because the test helper had no `cmake_minimum_required` (CI's older CMake replaced `@WORK@` in the helper itself). Crosscheck #19 (`Crosscheck-2026-11-10.md`): reviewer subagent on the code, Claude on Spec ↔ Plan ↔ Tests ↔ docs; AA1 (state text not UTF-8 throws), AA2 (`instrument` ok with the old instrument), AA3–AA10 low, AA11 docs.

User: "yes fix AA1–AA11, AA1 first" → P25c, with the helper fix. Spec v0.53.
