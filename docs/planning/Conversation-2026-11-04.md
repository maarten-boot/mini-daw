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
