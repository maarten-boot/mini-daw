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

## 3. P26a: windows on top of each other, "Project note:" label

User: "pushed it, yes fix both, Tabs window first".

- Not only Tabs: every own window opened at ImGui's one default place, so any two opened together covered each other. New `src/app/window_place.cpp`: `placeNewWindow(title)` before each own window's / dialog's Begin (13 places); the first time in a run a window opens at the first free place of a cascade (`cascadePlace`); later ImGui keeps where it was left. ImGui's hidden "Debug" fallback window is not counted; windows hidden in their first frame (fitted height) are (the first try missed them; seen on Xvfb, test made to catch it).
- Status line: "Project warning: …" (field `projectWarnings`).
- T-UI-31; Linux 222/222, MinGW under Wine 210/210. Spec v0.57, Plan P26a.

## 4. CI run 43599860783 (P26a)

User: "pushed it, check the CI run".

- windows, linux, licence green; Linux 222/222, Windows 212/212. Builds in `dist/`, `dist/TESTS.txt` with T-UI-31.

## 5. Crosscheck #21 (after P26 / P26a)

User: "pushed it, do the crosscheck".

- Independent reviewer on the code, Claude on Spec / Plan / Tests / docs and the reviewer's claims at the code. `Crosscheck-2026-11-14.md`: CC1 (medium: a script run with the normal config changes `[project] last`, so the next start loses the state), CC2–CC6 (low: windows list dropped on a project change, synced write on the UI thread, symlinked config replaced, minimised editor place on Windows, position without a stream), CC7 test gaps, CC8 docs. One reviewer point refuted (Open take folder closes all editor windows).

## 6. Crosscheck #21 fixes (P26b)

User: "pushed it, yes fix CC1–CC8, CC1 first".

- CC1: script runs leave `[project] last` / Recent alone; checked for every script case. CC2/CC6/CC7: restore decisions moved to core with tests; own windows kept; no position without a stream. CC4: a linked config stays a link, permissions kept. CC5: an editor's place kept while minimised. CC3: measured (3–6 ms), no change. CC8: "Project warning" seen on Xvfb; Plan texts.
- Linux 226/226; MinGW under Wine 213/213. Spec v0.58, Plan P26b.
