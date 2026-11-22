# Getting started with mini-daw

<!-- Made from src/core/src/tutorial.cpp: change the text there; the test T-UI-35 fails while this file differs (it writes the new text to the build folder). -->

A short tutorial for beginners in four lessons. The same lessons are in mini-daw under **Help > Getting started**, where each step ticks itself off when you have done it. On a Mac, the shortcuts are Cmd instead of Ctrl (Cmd+R, Cmd+L, ...).

## 1. Set up and record your first take

Choose where the sound goes in and out, make a project, and record yourself on one track. Use headphones: they keep the speakers out of the recording.

1. Choose your output (speakers or headphones) and your input (microphone, line in or audio interface), then Apply. The Output meter moves when something plays.  
   *Where:* Settings > Audio devices...
2. Make a project: give it a name, mini-daw makes a folder for it. Everything you record goes there, and it is saved by itself.  
   *Where:* File > New project...
3. You need an audio track: at the first start there is one already, or add one. Choose its input in the Input column (the channel your microphone or instrument is on).  
   *Where:* Recording tab > Add audio track, Input
4. Tick Arm on the track: it will be recorded. Sing or play: its Level meter moves. Keep it out of the red (CLIP).  
   *Where:* Recording tab > the track's Arm
5. Press Record, play or sing a few seconds, then press Stop recording. The take is saved in the project's folder; the track's Take column names it.  
   *Where:* Record (Ctrl+R), then Stop recording (Ctrl+R)
6. Go back to the start and press Play: you hear what you recorded.  
   *Where:* |< Start (Home), then Play (Space)

**Tip:** No sound in? Check Settings > Audio devices... and the track's Input. The line under the transport says what mini-daw is doing; View > Log (Ctrl+L) has the details.

## 2. MIDI and an instrument

A MIDI keyboard plays a VST3 instrument (a software synth or piano). mini-daw records what you play (the notes) and can turn it into sound.

1. Let mini-daw find your instruments: Scan. They are listed with their maker. (Instruments in another folder: Add folder, then Scan.)  
   *Where:* Settings > Instruments (VST®3)... > Scan
2. Add a MIDI track, choose your keyboard as its Input and an instrument in its Output column. A track "... sound" appears below it: there the instrument's sound will be recorded.  
   *Where:* Recording tab > Add MIDI track
3. Press Edit: the instrument's own window opens. Choose a sound (preset) there and play: you hear it at once.  
   *Where:* The MIDI track's Output column > Edit
4. Arm the MIDI track (and disarm the audio track), Record, play a few bars, Stop recording. The MIDI track's take holds your notes, the "... sound" track the instrument's sound.  
   *Where:* Arm, then Record (Ctrl+R)
5. Render: the instrument plays the MIDI take again, faster than real time, and its sound becomes the "... sound" track's take. Use it after you chose another sound.  
   *Where:* The MIDI track's Output column > Render
6. Choose what plays: "Sound from: instrument" (the instrument plays your notes) or "Sound from: this recording" (the recorded sound). Only one plays, so nothing sounds twice.  
   *Where:* The "... sound" track's Input column

**Tip:** No instrument found? Most install into C:\Program Files\Common Files\VST3 (Windows) or ~/.vst3 (Linux). An instrument that fails or crashes is listed with the reason and skipped.

## 3. Overdub and mix

Record a second part while the first one plays, then set the levels.

1. Add another audio track (or use the first one again), tick Monitor to hear yourself, and Arm only this track.  
   *Where:* Recording tab > Add audio track, Monitor, Arm
2. Go back to the start and Record: the other tracks play while you record, so you can play along. Stop recording when done.  
   *Where:* |< Start (Home), then Record (Ctrl+R)
3. Play it all, and set the mix: Volume, M (mute) and S (solo) per track. Volume, mute and solo count when you export a mix.  
   *Where:* Recording tab > Volume, mute, solo
4. Every Record take (R) and Auto record part (A) is in the Timeline, in the order you made them. Click one and Play it, or give it a name and notes.  
   *Where:* Timeline tab > a recording > Play it

**Tip:** Auto record (next to Record) waits for you to play and records each time you do: handy for practising. Its parts are in the Timeline too.

## 4. Save and share

Your project saves itself. To give your music to others, export it.

1. Open a project again later from the File menu; the last one opens when mini-daw starts.  
   *Where:* File > Open project... / Recent projects
2. Recordings are made smaller after they end: the WAV file becomes FLAC, the same sound in about half the space. On by default; older recordings with Compact recordings...  
   *Where:* Settings > Recording..., File > Compact recordings...
3. Export audio: the song (as Play plays it) or recordings from the Timeline, as one mix or a file per track (stems), as Ogg Vorbis, Opus, FLAC or WAV, optionally in a zip with the MIDI files.  
   *Where:* File > Export audio...
4. Export DAWproject: the whole project, to open in another music program (e.g. Bitwig Studio, Cubase or Studio One).  
   *Where:* File > Export DAWproject...
5. Words you do not know are explained in the Terms tab, with a field to find them.  
   *Where:* Terms tab

**Tip:** Larger text: Ctrl+= (smaller Ctrl+-). Higher contrast: Ctrl+Shift+Up, or View > Contrast.

