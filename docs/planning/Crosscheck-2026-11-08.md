# Crosscheck #15 — 2026-11-08 (after PL1: state cues, disabled controls, Output column, busy message)

Scope: Spec v0.41, Plan, Tests, README, Demo-checklist, CLAUDE.md, code at commit `7188ddd` (changes since `a6b9ba6`). Spec ↔ Plan ↔ Tests ↔ docs by Claude; the code also read by an independent reviewer (a subagent given the code and the Spec, not Claude's conclusions). Claude checked the findings at the lines named: W1 (`main.cpp:741-760` MenuItem / BeginMenu with `enabled = false`; the hook only records items inside `beginDisabled`), W2 (`imgui_widgets.cpp:1295`: a checked checkbox fills with `CheckboxSelectedBg`, not flattened; `imgui.cpp:8332`: the title bar is reported as an item; the scrollbar too) confirmed in the ImGui v1.93.0 sources. CI run 42527512800 green (Linux 177/177, Windows 178/178).

| Area | Verdict |
|------|---------|
| Audio thread | ✅ nothing new reaches the callback |
| Hook (window pointers, draw lists, clip) | ✅ cues live one frame, drawn before Render, `WasActive` checked, cell clip kept; ⚠️ one-frame lag **W3** |
| Disabled look | ⚠️ ImGui's own disabled menu items have no cue (at Max they look enabled) **W1**; a checked disabled checkbox keeps its fill **W2** |
| Contrast pairs | ⚠️ drawn but not checked: Border on Control (checked disabled checkbox), Text / TextDisabled / Border on Selection (disabled selected entry), Accent on the scrollbar; frames on title bars and scrollbars not in the Spec **W2** |
| beginDisabled / endDisabled | ✅ balanced on every path; ⚠️ only an `IM_ASSERT` guards a stray `endDisabled` **W7** |
| choice() | ✅ IDs stable, bar in Accent; ⚠️ "> " and "  " differ in width, entries shift **W6** |
| Busy message | ✅ timing, captures (values, track looked up again), guards inside openProject / createProject / applyAudioSettings; ⚠️ second request dropped silently, no recording guard in the instrument job **W4**; long text not wrapped **W5** |
| Track table | ✅ 10 columns, Output, freeze, linkRequest removal equivalent; ⚠️ height computed by hand (correct today) **W8** |
| Spec ↔ code ↔ docs | ⚠️ **W10**, **W11** |
| Spec ↔ tests | ⚠️ **W9** |

## Findings
| # | Sev | Finding | Proposed action |
|---|-----|---------|-----------------|
| W1 | medium | Menu items disabled by ImGui itself (`MenuItem(…, enabled)`, `BeginMenu(…, enabled)`: New / Open / Save as / Close project while recording, a Recent project not found) get no dashed frame: the hook only marks items inside `beginDisabled`. At Max (no fade, TextDisabled black) they look like enabled ones. | Dashed frame for every disabled item; the "box only" shrink only for checkboxes in `beginDisabled` blocks (ImGui reports every MenuItem as checkable). |
| W2 | medium-low | Pairs drawn but not checked: a checked disabled checkbox keeps its Control fill under the dashed Border frame (Border on Control is not a pair); a disabled but selected entry ("Sound from: this recording") draws Text, TextDisabled and the dashed Border on Selection; the hover frame is also drawn on title bars and scrollbars (Accent on ScrollbarTrack / Thumb not checked; not in the Spec). | Flatten `CheckboxSelectedBg` too; add the Selection pairs (normal and disabled); no hover frame on title bars and scrollbars (they are not controls a click acts on). |
| W3 | low | The hook runs before ImGui decides the hover, so a frame appears one frame (≈16 ms) late and stays one frame after the mouse leaves. | Record every enabled item with an ID and decide at draw time from the final `HoveredId` / `ActiveId` of the frame. |
| W4 | low | `startBusy` ignores a second request without a sign (the list has closed, the click seems lost); the instrument job does not check for a recording or a render of that track (unreachable today, but the old code ran inside the guarded table). | Log the dropped request; the instrument job returns (with a log line) while recording or while this track renders. |
| W5 | low | The busy text is not wrapped: "Opening project <long path>" runs out of a narrow window. | Wrap at the window width (less padding). |
| W6 | low | `choice()` puts "> " or two spaces in front; a space is narrower than ">", so entries move sideways when selected. | Same indent for every entry; the ">" drawn in that gutter. |
| W7 | low | Dashed frames are one `AddLine` per dash (a disabled track row ≈ 200 lines); a stray `endDisabled` is only caught by `IM_ASSERT` (compiled out in Release: undefined behaviour). | Longer dashes (fewer lines); a runtime check that logs and ignores a stray `endDisabled`. |
| W8 | low | The track table height is computed by hand (rows of frame height); a taller cell added later would cut off the last row (no vertical scrollbar with ScrollX only). | Comment at the computation, and a debug check that the real content height fits (logged once if not). |
| W9 | tests | T-UI-24 checks "no fade" only for Max, not for AAA (Spec: AAA and above none); nothing tests `state_cues` (hook cues, disabled balance, `choice` IDs). | AAA check; a headless ImGui test (context without a renderer, one frame): cues for hovered / disabled items, no cue for title bar / scrollbar, balance after nested blocks, `choice` ID stable. |
| W10 | docs | SPEC-UI-15: "a disabled checkbox: a dashed box" and "every control … disabled" do not hold yet (W1, W2); the one-frame lag and the title-bar / scrollbar frames are not mentioned. | Spec after the fixes. |
| W11 | docs | CLAUDE.md layout lists only `main.cpp`, `imgui_theme.cpp` and the renderers under `src/app/` (also missing `folder_chooser`, `plugin_window`, now `state_cues`). | Complete the list. |
