# Conversation log — 2026-11-04

Continues `Conversation-2026-11-03.md` (P10a–c, e, the CI hang fix and the user's first Windows test of instruments were done there).

## 1. CI after the meter fix
> pushed it, check the CI run
- Run 41734020005 (`23bbdfc`, level meter fixed width): windows, linux and licence green.

## 2. P10d: plugin editor windows
> start P10d
(started late on 2026-11-03)

- Core: `InstrumentEditor` (in `instrument.hpp`): creates the plugin's view for a platform, content scale, size, resizable, attach, resize within the view's limits, the view's own resize requests (IPlugFrame), close; one editor per instrument; unloading the instrument closes it. The SDK's `commoniids.cpp` added for the view interface IDs.
- Test synth: an editor (a plain Win32 window: gain as text, click left/right to change it through a real edit), resizable 200 × 100 .. 1200 × 800, plus a window-less "minidaw-test" platform so the host logic is tested on Linux too; it shows the scale it got in a hidden parameter.
- GUI: `plugin_window.cpp`: one GLFW window per editor (no graphics API, owned by the main window, plugin size in pixels, size and scale callbacks). An Edit / Close button per instrument track; closed on instrument change, track removal, project switch, quit; reopened after a sample rate change.
- Tests: T-VST-18 (all systems), T-VST-19 (Windows window, click → sound), T-VST-20 manual. Linux 128/128, Wine 128/128; live under Wine with xdotool (see Tests.md). Spec v0.26 (SPEC-VST-05a).
- Found on the way: the Edit button was cut off in the narrow Take column (moved in front of the list); quitting under Wine takes 30–45 s, also with the previous build (Wine only). Linux editors need a VST3 run loop for X11: later (P12).
- Not tried here: real plugins' editors (Arturia etc.); for the user on Windows (T-VST-20).

## 3. CI after P10d
> yes commit it, i'll push
- Run 41736545593 (`abdf558`, P10d): windows, linux and licence green. Windows 130/130 in 31 s, including T-VST-18 and T-VST-19 (the editor in a real Windows window, click → sound) on the real runner.
- `dist/` holds the `abdf558` build: Windows exe/com (MinGW), Linux from the Ubuntu 24.04 container (128/128 there).
> prepare for the last task; going sleeping
- Unclear which task is meant; nothing new started. Waiting for the user: T-VST-20 on Windows, then P11 or crosscheck #9.

## 4. Crosscheck #9
> do the crosscheck #9
- Written up in `Crosscheck-2026-11-02.md` (Claude + independent reviewer; every reviewer claim checked against the code, one proposed fix disputed: Q5).
- Medium: Q1 `stable_sort` allocates in the audio callback; Q2 a module that crashes in its first scan is not recorded (crash loop); Q3 hung notes on disarm / slot shift, per-slot state kept; Q4 parameter changes beyond 4 per block dropped; Q6 take timing after a rate change. Low: Q5 restart requests, Q7 needless reloads, Q8 wrong-typed instrument field, Q9 echo. Docs Q11, tests Q12, notes Q10.
- Waiting for the user's go.

## 5. Crosscheck #9 fixes
> yes fix Q1–Q12, Q1 first
- Q1 + Q3 together (the same loop): no sort; per-instrument state that follows the instrument; live notes released on disarm; gain in the configuration; note-offs kept when the block is full. Then Q2, Q4/Q9, Q12 (test synth: CC 7 mapping, controller state, restart hook), Q6/Q7, Q5, Q8, Q11. Details in `Crosscheck-2026-11-02.md`.
- My first Q4 version would have allocated (a new offset adds a point): changed to reuse the last point's offset before testing.
- Tests T-VST-21..23, T-LIC-06; T-VST-11 and T-VST-15 extended; T-VST-22 and T-LIC-06 checked to fail without their fixes. Linux 131/131, Wine 131/131.
- The Wine prefix in /dev/shm was gone (cleared since last night) and was made again; its first test run had one failed assertion (not captured), 8 runs after that passed.
