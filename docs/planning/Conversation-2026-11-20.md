# Conversation log — 2026-11-20

## 1. Look at the project, old CI artifacts

User: "inspect the current directory", then "list the old CI artifacts to remove".

- State: version 0.28.4 (P25f done), working tree clean, last CI runs green. Next planned step: P29 (export audio), which still has open questions.
- The repository has no releases, so the release / artifact rule does not apply. But there are 190 CI artifacts, and 52 of them are older than 14 days: created 2026-11-01 to 11-05, from 30 runs, 59.4 MB.
- This session cannot write to GitHub, so the user deletes them. User: "show me a script i can run to delete, your session has no write access to github".
  - `tmp/delete-old-artifacts.sh` holds the 52 artifact IDs. Run without arguments it is a dry run; `--delete` deletes them. It checks each artifact again before deleting it, and deletes only artifacts (no runs, logs or tags).
  - `tmp/old-artifacts.tsv` lists them with ID, date, run, name and size.
- User: "note any file ouside of this directory tree is not vizible on other hosts where we have write access so create scripts relative to this directory tree, e.g. in ./tmp".
  - The script and the list were moved from the session scratchpad to `./tmp/` (gitignored).
  - From now on, files for the user go in `./tmp/`.
- User: "done, check the artifacts are gone". Checked: 138 artifacts left, the oldest from 2026-11-06 12:15; none of the 52 IDs is still there. `tmp/delete-old-artifacts.sh` and `tmp/old-artifacts.tsv` removed.

## 2. P30: Terms at the right end, the timeline in lines, one transport

User: "can we move the terms tab to the far right ?; In the timeline of recordings we currently append each take to the right and pan the view, can we change that to: when we have reached the right side of the screen we and a new line under the current and continue there and when we have reached the bottome we scroll the view and continue down. We currently reprat the Transport on 2 views , would it make sense to create a separate view for that ?"

- Read as "add a new line", "bottom", "repeat".
- Choices asked:
  - Terms: pinned at the right end (not only last in the order) — user chose pinned.
  - Timeline: user asked "can they overlap ?". No: Record is off while auto record is on (SPEC-REC-08), so auto parts and takes never overlap in time. User chose one stream of blocks, A / R per block.
  - A recording longer than a line: it goes on in the next line (user's choice).
  - Transport: a strip above the tabs, on every tab (user's choice; an own tab was advised against, since the transport and the tracks could not be seen together).
- Done (P30, version 0.30.0; P29 done later will raise the round):
  - core: `layoutTimeline` one stream (nothing on top of anything), `wrapTimeline` (lines like text; short pauses left out at a line's start; gaps never cut; a long block cut into pieces), `isPinnedTab` and `normalizeTabOrder` with Terms always last.
  - GUI: Terms with ImGui's trailing flag plus an invisible spacer tab in front of it (a trailing tab alone only stays in view, it is not moved to the right end); Settings → Tabs… without buttons for Terms. The timeline fills the tab (at least three lines), scrolls down, follows the last line; "..." on continued pieces; marks in their piece; takes on the Table header background. Gap markers as wide as their longest label (seen on Xvfb: "+1 h 00 min" ran into the next block inside a line).
  - The transport is drawn once, above the tab bar.
  - Terms texts "Transport" and "Timeline" (the latter also said "Click a recording to hear it": it is a double-click).
  - Spec SPEC-PLAY-07, UI-10 (the area is 8 lines, it said 7), UI-11, UI-12; Plan P30; Tests T-PLAY-21, T-UI-33, T-UI-34; README.
- Linux 257/257; GUI checked on Xvfb. Not run: MinGW under Wine.
- To consider: the transport's fixed 8-line info area is now above every tab, which leaves less room below in a small window.
