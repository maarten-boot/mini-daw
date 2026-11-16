# Conversation log 2026-11-16

## 1. Auto record parts in the export (P27b); start P28

User: "one \"Auto record\" track; pushed; start P28".

- P27b: auto record parts that no track plays go on one muted "Auto record" track, a lane per file, every clip at 0 s (where a part goes when loaded onto the tracks); Record takes no track plays stay left out with a line. The clip writer is now shared by tracks and parts.
- Found while doing it: the Timeline's list of recordings was only read while the Timeline tab was drawn, so an export without opening the tab could miss parts (and P27's left-out count could be wrong); the export now reads it again.
- Linux 237/237; the XSD check over 6 exports.
