# Conversation log — 2026-11-07

Continues `Conversation-2026-11-06.md` (window place, auto record P16, crosscheck #11 and its fixes).

## 1. CI of `cc835cd`
- The background check had stopped with the session; checked again: run 42304847985 green (windows, linux, licence).

## 2. Tabs and a timeline of recordings (P17)
> continue; an additional idea: make the recording setup the first tab of a notebook and allow (e.g. when we are in auto mode) that the user can switch to a second tab where the timeline of the recordings is shown (time gaps between recordings can be shortened and are not in real-time distance).
- Read as: tabs "Recording" (the screen as now) and "Timeline"; asked three questions. Answers: show auto parts and takes; a click plays it and it can be loaded onto the tracks; long pauses drawn short with their real time.
- Core `timeline.hpp`: `listRecordings` (take folders and `auto/` parts, start from the name, length from the files), `layoutTimeline` (to scale up to 10 s pauses, longer ones a marker "+12 min"), `gapLabel`, `clockLabel`, `assignTakeFolder` (files onto the tracks with the same number and kind; the tracks keep their instruments). GUI: tab bar; Timeline tab with the transport, two rows of blocks (buttons, "> " for the selected one), zoom, Refresh, details with Play it / Stop and Load onto the tracks; a recording plays on its own (preview), the transport shows its length.
- Found in the GUI check: labels cut off; the pause label measured from the drawn block (+14 s instead of +51 s); the transport length while previewing. All fixed; tests added.
- Spec v0.34 (SPEC-UI-11, SPEC-PLAY-07), Plan P17, T-UI-17, T-PLAY-18. Linux 164/164, Wine 164/164.

## 3. CI of `d94be3d`, crosscheck #12
> pushed it, check the CI run and do the crosscheck after that
- Run 42386124740 green (Linux 164/164, Windows 166/166); its Windows build is in `dist/windows`.
- Crosscheck #12 (Spec v0.34, code at `d94be3d`; the code also read by an independent reviewer, every finding checked at its lines): T1 Record / Auto record / Home during a preview used the preview's position; T2 a Record take was not shown while recorded; T3–T12 smaller (scrolling, cost per frame, song position, warnings, daylight saving, name parsing, "+60 min", overlapping blocks, empty folders, Load during a render); T13 docs, T14 tests.

## 4. Pinning
> When using github actions or other action steps, pin them on the version hash and try to use the latest workable version. Explain when that is not possible.
- The workflow's three actions were already pinned by hash and are the newest releases (hashes checked against the tags). Not pinnable by hash: runner images, Ubuntu packages, NSIS from Chocolatey (pinned by version). The libraries in `cmake/Dependencies.cmake` were pinned by tag; offered to pin them by hash. Saved as a working rule.

## 5. Fixes T1–T14 and the library pins
> yes fix T1–T14, T1 first, and pin the dependencies
- All fixed (see Crosscheck #12, actions). The GUI check found that the new empty-folder rule hid the MIDI take being recorded (no file until it ends): the folder being recorded is now added to the list.
- Libraries pinned by commit hash; updated to RtMidi 6.1.0 (classes moved into `rt::midi`, one forward declaration changed), Dear ImGui v1.93.0, GLFW 3.5.1. The licence check requires the hash.
- Spec v0.35 (SPEC-PLAY-07 texts, SPEC-CI-04), Plan P17a, T-UI-17 extended, T-PLAY-18 extended, T-CI-03. Linux 167/167, Wine 166/166, GUI on Xvfb.

## 6. CI of `572d2b5`
- Run 42388802838 green (Linux 167/167, Windows 168/168 with the new libraries); its Windows build is in `dist/windows`. Logged (`169e75f`).

## 7. Audio setup tab and tab order (P18)
> commit the CI log lines; looks good so far; now that we have tabs would it make sense to move the audio settings and devices to a tab, they are not often used and are mainly for infrequent reconfigurations i would guess. Possibly add a functionality in the settings to customize the sequence of the tabs so the user can customize
- Read "mive the audio sessings" as "move the audio settings". Asked four questions. Answers: move both "Audio stream" and "Devices", keep a status line on the Recording tab; name "Audio setup"; order by dragging and in a Settings dialog; at start the tab used last.
- Config `[tabs] order / last` (`normalizeTabOrder`), saved when they change. The tab bar uses ImGui's reorderable tabs; the order after a drag is read from ImGui's tab bar (internal API: `GetCurrentTabBar`, `TabBarGetTabName`; ImGui only re-sorts tabs by submission order when a tab is new). Settings → Tabs… (Up / Down, Default order) starts a new tab bar with the new order. Driver messages are now collected every frame whatever tab is shown.
- Spec v0.36 (SPEC-UI-11 changed, SPEC-UI-12), Plan P18, T-UI-18. Linux 168/168, Wine 167/167; GUI on Xvfb (status line, link, drag, Tabs window, restart).

## 8. Windows results; text size
> when changing the text size it is not applied directly (was that ok) T-UI-15 now ok; T-UI-16 is now OK; the rest i will test later
- T-UI-15 (maximised start) and T-UI-16 (no jumping) pass on Windows.
- The text size was not OK (SPEC-UI-08 says at once): ImGui sets `style.FontSizeBase` back to the size in use during a frame, so a change from Ctrl+= or View → Text size was only saved and applied at the next start. Not caused by the ImGui update (v1.92.9 does the same). Fixed by also setting `_NextFrameFontSizeBase`, as ImGui's own style editor does; checked on Xvfb (16 → 20 px at once). T-UI-12 now says "at once".

## 9. CI of `cc9a41d`, crosscheck #13
> do the crosscheck after P18; note setting fonts do not react (or did i say that already)
- Run 42405885386 green (Linux 168/168, Windows 169/169); its Windows build is in `dist/windows`. Logged (`fc3b1b2`).
- "Setting fonts do not react" read as the text size report of section 8 (the only font setting): fixed in `cc9a41d`, in the new build.
- Crosscheck #13 (Spec v0.36, code at `cc9a41d`, independent reviewer as in #12): U1 keyboard navigation off (the Tabs dialog promises keyboard use); U2 first frame of a tab bar shows and saves the first tab; U3–U5, U8 small (duplicate button ID, focus after Up, stalled hint, notes list without limit); U6 tests; U7 docs.

## 10. Crosscheck #13 fixes (P18a)
> yes fix U1–U8, U1 first
- U1: keyboard navigation on in every window; Space and Home are owned by mini-daw each frame so they stay Play / Stop and To start (ImGui's navigation would otherwise press the focused control with Space). New SPEC-UI-13, T-UI-20.
- U2–U8 as proposed (see Crosscheck #13, actions). The keyboard GUI test of the Tabs dialog found a new bug in the U4 change (one Down press moved a tab two places); fixed by moving after the rows are drawn.
- Spec v0.37, Plan P18a, T-UI-18 extended, T-UI-19, T-UI-20. Linux 169/169, Wine 168/168; GUI on Xvfb.

## 11. Linux build in dist/; Browse buttons (P19)
> when you build a test windows exe also build a linux fresh please, the dist/*/files are now of a different timestamp
- `dist/linux` was from 2026-11-05. Now all of `dist/` is emptied and both CI builds of one run go in (Windows and Linux, run 42411813163); saved as a working rule (memory).
> when adding a vst folder do i have to type the path or is there a file browser that i can use to find and accept an existing path ?
- Answer: typed only, and not checked. Proposed Browse… buttons for every folder field and a check when adding.
> yes do P19 with browse buttons; and probably a way to remove folders as well
- There was a remove button ("X"), but after the path: for a long path outside the window (likely why it was not seen). Now **Remove** in front of each added folder.
- Browse… on the five folder fields: Windows uses the system "Select folder" dialog on its own thread (mini-daw keeps running); Linux uses a folder browser drawn in mini-daw. Adding checks the folder (exists, not a file, not standard, not listed, at most 8; remark when no `.vst3`). SPEC-UI-14, SPEC-VST-01b, Spec v0.38, Plan P19, T-UI-21–23, T-VST-32.
- Under Wine the audio stream did not start, which showed the stopped-stream line (left open in T-UI-18): its Audio setup button was outside the window; now in front of the text. Linux 171/171, Wine 170/170; GUI on Xvfb and Wine.

## 12. CI of `a2e4458`, crosscheck #14
> pushed it, do the crosscheck after P19
- Run 42419274839 green (Linux 171/171, Windows 172/172); both builds in `dist/`. Logged (`fcb075b`).
- Crosscheck #14 (Spec v0.38, code at `a2e4458`, independent reviewer): V1 a folder name that is not UTF-8 ends mini-daw on Linux (reproduced); V2 closing mini-daw while the Windows dialog is open may leave it running (Windows, unverified); V3 adding a whole disk stalls the window; V4 shortcuts act behind the browser; V5–V9 small; V10 tests; V11 docs.

## 13. Crosscheck #14 fixes (P19a)
> yes fix V1–V11, V1 first
- V1–V11 as proposed (see Crosscheck #14, actions). The new tests found that an unreadable folder looked empty; fixed. Closing with the Windows dialog open checked under Wine only partly (taskkill also closes the dialog); the real case is in T-UI-23.
- Spec v0.39, Plan P19a. Linux 173/173, Wine 172/172; GUI on Xvfb and Wine.
