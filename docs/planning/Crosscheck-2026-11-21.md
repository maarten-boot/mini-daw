# Crosscheck #25 — 2026-11-21 (after P30 / P30a: Terms pinned right, the timeline in lines, one transport)

Scope: Spec v0.70, Plan, Tests, README, CLAUDE.md, Terms texts, code at `ed4703f` (P30a). Spec ↔ Plan ↔ Tests ↔ docs by Claude. The code changes (`git diff 2ea4c2a ed4703f -- src tests`) were also read by an independent reviewer: a subagent given the diff, the intended behaviour and the ImGui sources, not Claude's conclusions. Claude checked its findings against the code.

| Area | Verdict |
|------|---------|
| Terms pinned (SPEC-UI-12, T-UI-33) | ✅ `normalizeTabOrder` puts Terms last on load, on save and in `setTabOrder`. The spacer width matches ImGui's layout. The spacer (`##tab-spacer`) is not in the drag read-back, and no tab can be dragged past it or past Terms. Settings → Tabs… bounds use the movable tabs. ⚠️ cosmetic: after a resize Terms is one frame late (ImGui lays out with the spacer width of the frame before). |
| Timeline wrap (SPEC-PLAY-07, T-PLAY-21) | ✅ `wrapTimeline` always ends. Pieces of a block are grouped. PushID / PushStyleColor / popup are balanced. There is one menu per block, marks at a cut are neither lost nor counted twice, the caches rebuild when needed, and following the last line works. ⚠️ a very short last piece **CC1**; floating point at the last cut **CC2**; a gap marker left alone at a line's end, or wider than a very narrow line (clipped) **CC4**. |
| Transport above the tabs (SPEC-UI-10, -11, T-UI-34) | ✅ drawn once, before the tracks and the timeline in the same frame. The note field's focus works on every tab, and the transport keys don't depend on the tab. ⚠️ with 4 lines, a disk error while recording is below the visible part **CC3**. |
| Spec ↔ Plan ↔ Tests ↔ docs | ✅ SPEC-PLAY-07 / UI-10 / UI-11 / UI-12 ↔ P30 / P30a ↔ T-PLAY-21, T-UI-33, T-UI-34. README, Terms texts and the version (0.30.1) agree. ⚠️ stale lines **CC5**. |

## Findings

| # | Sev | Finding | Proposed fix |
|---|-----|---------|--------------|
| CC1 | medium | `wrapTimeline` keeps a minimum width only for the first piece of a cut recording. Its last piece can be a few pixels wide. The button is drawn `width - 2` wide, and ImGui reads a width ≤ 0 as "label width" or "up to the right edge". So a last piece of ≤ 2 px becomes a wide button that covers the line. This happens with about 2 px in every line width, more often in small windows or at high zoom. | In `wrapTimeline`, a rest shorter than `minPiece` is not put on a line of its own. The cut is made earlier, so the last piece is at least `minPiece` wide. Also draw at least 1 px. Add a test. |
| CC2 | low | The cut loop adds `done += w`. In floating point the last piece's `to` can end just below 1.0, or a piece of about 1e-13 px can be added on a new line. A mark at the very end (share 1.0) is then lost, and the tiny piece hits CC1. | Snap the last piece: end = the block's width when `done + w` reaches it, and `to = 1.0` exactly. Add a test with widths that do not add up exactly. |
| CC3 | low–medium | P30a: with 4 lines, the order while recording is RECORDING line, "Note at" field (taller than a line), "Into:" path (2 lines when long), and then `Error:` / `Not recording:`. A disk error, which loses data, is then only seen after scrolling the area. The same goes for "Lost in parts" and "Error:" in auto record. | Show `Error:` / `Not recording:` / lost counts directly after the RECORDING / AUTO line, before the note field. Or add a short "ERROR" to the state text in the button row ("RECORDING – ERROR, see below"). |
| CC4 | low | A gap marker that fits at a line's end stays there while the block after it goes to the next line. In a very narrow window a gap wider than the line is clipped; there is no sideways scrollbar any more. Cosmetic. | Keep a gap together with the block after it (if both don't fit, both go to the next line). Make the line at least as wide as a gap. |
| CC5 | doc | Tests.md T-UI-17 still expects "The transport is on both" tabs, and T-PLAY-18 says "two recordings in the same second in one row". The Plan PX row has no #25. CLAUDE.md's version example says "P29 → 0.29.0", but P29 done after P30 will be 0.30.x. | Update these lines. |

Not taken up: when a block's right-click menu is open and the block is scrolled fully out of view, the menu is no longer drawn but stays open until the next click elsewhere. This was the same before P30.

Checked and fine (reviewer and Claude):
- `wrapTimeline` with zero, negative or too-small widths.
- The order of gaps and blocks (stable sort, gap first).
- The cache keys `layoutStamp` / `wrapStamp` / `wrapWidth`.
- `SetScrollHereY` after the end marker.
- The spacer label is not read back as a tab, and `TabItemCalcSize` matches the real tab width.
- `drawTransport` is called once.
- `markFocus` works on every tab.
- The config round trip of the tab order.

## Actions taken (user: "yes fix CC1–CC5, CC1 first"; P30b, version 0.30.2)

| # | Done |
|---|------|
| CC1 | `wrapTimeline`: if the last piece of a cut recording would be shorter than `minPiece`, the cut before it moves back so it is `minPiece` wide; the piece before keeps some width (less than `minPiece` only on lines narrower than two `minPiece`). The GUI draws every piece at least 1 px wide. Test: 401 on lines of 200 → 200, 171, 30. |
| CC2 | The cut places are computed first; the last piece ends exactly at the block's width with `to = 1.0`. Test: four widths that do not add up exactly in floating point, the pieces' widths add up, no extra piece. |
| CC3 | Under the transport: "Not recording: …" first, then the state line, a disk error and lost counts, then the note field, then overdub, folder, parts saved, last take, playback notes. Checked on Xvfb while recording. |
| CC4 | A gap goes to the next line when the block after it would. A line is at least as wide as the widest gap (in core) and as a block's minimum (in the GUI). A narrower window gets a sideways scrollbar. Tests for both. Also seen while checking: the "From … A = auto record part …" text beside the zoom slider became a tall narrow column in a 700 px window; it is now on its own line. |
| CC5 | Tests.md T-UI-17 ("the transport is above the tabs") and T-PLAY-18; CLAUDE.md's version example (P30 → 0.30.0, P29 after P30 → 0.30.<next round>); Plan PX #25. |
