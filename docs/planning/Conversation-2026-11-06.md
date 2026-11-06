# Conversation log — 2026-11-06

Continues `Conversation-2026-11-05.md` (Windows test with Arturia instruments, linked track at instrument choice, overdub hint).

## 1. CI of `3cf8f21`
- The background check had stopped with the session; checked again: run 42124769181 (`3cf8f21`) green on windows, linux and licence. Its Windows download (setup, exe, com) is in `dist/windows`.

## 2. Remember the window size and place
> add that by default when closing the app it remembers size and position on screen th stay the same on next open.
- Read as: on by default, the main window reopens with the size and position it had at close (I also keep "maximised", and a switch `remember = false` to turn it off).
- New `[window]` section in `config.ini` (remember, x, y, width, height, maximized), written at a normal quit. At start it is fitted to the screen it overlaps most; if it is not on any screen now (monitor gone) the default place is used. The window is created hidden and shown once placed (no jump).
- Core: `WindowSettings`, `ScreenRect`, `restoredWindowRect` (config.cpp); app: restore before the display-scale step, track the normal place every frame, save at quit.
- Spec v0.31 (SPEC-UI-09), Plan P15, T-UI-14 (automatic + Linux GUI), T-UI-15 (Windows, manual). Linux 139/139, Wine 139/139, GUI checked on Xvfb.

## 3. CI of `4f1a628`
> pushed it, check the CI run
- Run 42215581356 green on windows, linux and licence; its Windows download is in `dist/windows`.

## 4. Windows results: mute/solo, quit with the plugin open
> pushed it, check the CI run
- Run 42217180759 (`b1aac4b`) green.
> mute and solo tested and work … reopening and quitting with the plugin open keeps the plugin but i dont expect you want it to appear open after restart? we can press the edit again if needed?
- Agreed: the editor stays closed after a restart (SPEC-VST-05a unchanged); Edit opens it. Logged in Tests.md. Asked whether the preset was kept.
> yes the preset was kept
- T-VST-20 complete apart from resizing the window and another monitor's scaling (not reported); logged.
