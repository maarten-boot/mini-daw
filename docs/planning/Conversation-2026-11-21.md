# Conversation log — 2026-11-21

## 1. Commit P30; P30a: the transport's line area 4 lines

User (late on 2026-11-20): "yes commit, then shrink the transport area to 4 lines".

- P30 committed as `a918e0d` (version 0.30.0).
- P30a (version 0.30.1): the line area under the transport buttons is 4 lines high instead of 8 (SPEC-UI-10). It still scrolls when it needs more.
  - The playback notes ("the disk was too slow", files not played) now come after the other lines. Otherwise they could push the "Note at" line out of view while recording.
  - While recording, the 4 lines hold the RECORDING line, the "Note at" line and the take folder (2 lines when long).
- Linux 257/257. GUI on Xvfb (1150×750): the tabs moved up by about 80 px; while recording everything fits in the 4 lines.
- Spec (status v0.70, SPEC-UI-10), Plan P30a, Tests T-UI-34, README.
