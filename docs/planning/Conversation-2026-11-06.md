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

## 5. Windows: maximised start, render, jumping text; auto record idea
> ok tested: maximize -> stop -> restart; the whole screen becomes black with a white rectangle … then after about 2 seconds the full maximized app appears. best hide the initial black and white if possible. render after record works … the controls are a bit confusing … some text is also causing jumping while or after recording … we have multiple instruments and audio tracks but still only one 'song'.
- Fixed the start: the window stays hidden until the start-up work is done and is shown maximised in one step (Windows: SetWindowPlacement). Linux checked; Wine cannot show it (no window manager); the user checks on Windows.
- Jumping text: seen in my own GUI test too: the RECORDING lines and the "Last take" list appear above the track table and push it down. Proposed a fix.
- One song: yes, a project is one song; more songs = more projects (File > New / Open / Recent).
> I was thinking of an 'auto' record mode: when you press auto it simply records all you play and saves the part under a file name with a date-timestamp (automatically ending when playing stops)
- Asked to confirm the details before planning.
- Answers: auto record starts at your playing and ends after a pause; records the armed tracks; files only (date-time folders, tracks unchanged). Fix the jumping text now: yes.
- Done: fixed-height transport area, slider and Recording settings always shown, the table hint always shown (SPEC-UI-10, T-UI-16, checked on Xvfb). Auto record written as SPEC-REC-08 / P16 / T-REC-33 (first written as T-REC-32, which was taken: renamed), waiting for the user's go.
- T-REC-18 failed once during a parallel build: a test weakness (no read when the writer finished first), fixed in the test.

## 6. CI of `d3ffcbf`; P16 auto record
> pushed it, check the CI run
- Run 42288600626 green (windows, linux, licence); its Windows download is in `dist/windows`.
> start P16
- Recorder parts mode: the disk thread keeps the last 4 s of every take in memory; beginPart() opens the part's files and writes from up to 4 s back, endPart() ends exactly (cutting off what was already written after the end); nothing is written between parts. The audio thread notes when a take was loud.
- AutoPartTracker (pure, tested): a part starts 1 s before the playing was noticed (the earlier of the last look and the earliest new MIDI message), ends 1 s after the last activity once there has been none for the pause and no note or sustain pedal is held (60 s at most).
- RecordingSession::startAuto / autoUpdate: a folder `auto/<date time>` per part, the audio parts via the recorder, the MIDI parts written by the MIDI thread (refreshed every second while open); MIDI events no part needs are dropped.
- GUI: "Auto record" button, AUTO: WAITING / RECORDING, the last parts, settings for the pause and the threshold; Record and arming disabled meanwhile; Play allowed (the armed tracks' takes do not play).
- Found on the way: T-REC-32 was already taken (MIDI hot-plug): the new tests are T-REC-33. A part could ask to start before auto record was switched on: clamped (GUI test).
- Linux 149/149, Wine 149/149; GUI checked with the test synth and vkeys.
