# Conversation log 2026-11-16

## 1. Auto record parts in the export (P27b); start P28

User: "one \"Auto record\" track; pushed; start P28".

- P27b: auto record parts that no track plays go on one muted "Auto record" track, a lane per file, every clip at 0 s (where a part goes when loaded onto the tracks); Record takes no track plays stay left out with a line. The clip writer is now shared by tracks and parts.
- Found while doing it: the Timeline's list of recordings was only read while the Timeline tab was drawn, so an export without opening the tab could miss parts (and P27's left-out count could be wrong); the export now reads it again.
- Linux 237/237; the XSD check over 6 exports.

## 2. P28: DAWproject import

- Core `dawproject_import.cpp`: reads and checks the whole file before making anything; tracks (groups flattened, master / effect returns left out, nested lanes as tracks of their own), volume / mute / solo, all note clips of a track in one MIDI take, all audio clips rendered into one WAV at the stream's rate (playStart / playStop, loops, fades, Warps only at their own speed), VST3 instrument with its `.vstpreset` state, markers as marks of the one recording folder, title / comment; everything else listed as left out; a write failure removes what was written.
- Decided while building (Spec v0.63): several audio clips of a track become one rendered take (mini-daw has one take per track) instead of being left out as SPEC-PRJ-05 first said; notes likewise merged. Playback does not resample, so imported audio is converted to the stream's rate with a windowed-sinc filter (`sinc_kernel`); the first version (16 zero crossings) let 23 kHz fold back at −28 dB, now 24 crossings, cutoff 0.92 (−93 dB).
- App: File → Import DAWproject… (file with Browse… — the folder chooser got a file mode: the Windows Open dialog with a filter, or the ImGui browser listing `.dawproject` files), project name and folder; the new project opens. Script command `import`. Terms text.
- Linux 243/243, MinGW under Wine 226/226; the window by hand on Xvfb; a script import with "é" in file and folder names under Wine.

## 3. CI run 43849674053 (P28 + P27b)

User: "pushed it, check the CI run".

- windows, linux, licence green; Linux 243/243 (XSD over 6 exports, script `import`), Windows 229/229. Builds in `dist/`; `dist/TESTS.txt` with T-PRJ-11 (import a Cubase / Bitwig file) and T-PRJ-10b.
