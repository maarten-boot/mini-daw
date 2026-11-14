# Conversation log 2026-11-14

Continues `Conversation-2026-11-12.md` (log lines P25d / P25e, crosscheck #20).

## 1. P26: reopen in the same state

User: "ok add to the mini-daw that it remembers size and position when closed and that it reopens with the same size and position, and in the same state (that means if auto record was on, it will continue with auto record when restarted)".

- Claude: size and position (and project, tab) are already restored since P15. Asked what else: user chose instrument windows, song position and the other mini-daw windows; auto record also after a crash.
- Core: `SessionState` / `[state]` in `config.ini` (project, auto_record, position, editors track:x:y, windows), parsed with warnings, saved with the rest; the config is now written beside the old file, synced and renamed (`writeFileReplacing`).
- App: auto record saved when its button switches it (`rememberAutoRecord`); everything captured at a normal close before anything stops; restored once in the first frame (`restoreSessionState`): own windows always, the rest only with the same project; instrument windows at their place if on a screen (`PluginWindows::open(..., place)`, `position()`); if auto record cannot start, the transport says why and it stays on. Scripts neither restore nor save the state. Terms "Auto record" mentions it.
- Tests: T-UI-29 (config); T-UI-30 by hand on Xvfb incl. `kill -9` after switching auto record off and on. Seen: the Tabs window opens behind the Log window (same default place).
- Spec v0.56 SPEC-UI-09a; Plan P26; README.

## 2. CI run 43598345181 (P26)

User: "pushed it, check the CI run".

- windows, linux, licence green; Linux 220/220, Windows 210/210. Builds in `dist/`, `dist/TESTS.txt` with T-UI-30.
- Corrected T-UI-30's "another project" step: a normal close saves the open project's state, so "Not restored" needs another project and an end without a normal close (Task Manager).
