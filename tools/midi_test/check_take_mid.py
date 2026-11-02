#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the timing of a MIDI take recorded by mini-daw (T-REC-01 / T-REC-04).

Prints the note-on times, the gaps between them and the note lengths, and
compares them with what make_test_mid.py sent. The absolute start is not
known (the sender starts at an arbitrary moment), so only spacing and
lengths are checked.

    tools/midi_test/check_take_mid.py "/tmp/takes/take-*/02-MIDI 2.mid" --gap-ms 500 --length-ms 100

Exit code 1 if a gap or length is off by more than --tolerance-ms (2).
"""
import argparse
import glob
import struct
import sys


def parse_track(data):
    i, tick, running, out = 0, 0, 0, []
    while i < len(data):
        v = 0
        while True:
            b = data[i]
            i += 1
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                break
        tick += v
        status = data[i]
        if status == 0xFF:
            length = data[i + 2]
            out.append((tick, "meta", data[i + 1], data[i + 3:i + 3 + length]))
            i += 3 + length
            continue
        if status < 0x80:
            status = running
        else:
            running = status
            i += 1
        n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
        out.append((tick, "ch", status, data[i:i + n]))
        i += n
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file", help="MIDI file (a glob pattern is allowed)")
    ap.add_argument("--gap-ms", type=float, default=500.0)
    ap.add_argument("--length-ms", type=float, default=100.0)
    ap.add_argument("--tolerance-ms", type=float, default=2.0)
    a = ap.parse_args()

    files = glob.glob(a.file) or [a.file]
    data = open(files[0], "rb").read()
    fmt, ntracks, division = struct.unpack(">HHH", data[8:14])
    tracks, pos = [], 14
    while pos < len(data):
        length = struct.unpack(">I", data[pos + 4:pos + 8])[0]
        tracks.append(data[pos + 8:pos + 8 + length])
        pos += 8 + length
    smpte = division & 0x8000 != 0
    tps = 1000.0 if smpte else 1920.0  # mini-daw: SMPTE 25x40 or 960 PPQ at 120 BPM
    print(f"{files[0]}: format {fmt}, {ntracks} tracks, {'SMPTE 1 ms' if smpte else f'{division} PPQ'} ticks")

    events = parse_track(tracks[1])
    on = [(e[0] / tps * 1000, e[3][0]) for e in events if e[1] == "ch" and e[2] & 0xF0 == 0x90 and e[3][1] > 0]
    off = {}
    for e in events:
        if e[1] == "ch" and (e[2] & 0xF0 == 0x80 or (e[2] & 0xF0 == 0x90 and e[3][1] == 0)):
            off.setdefault(e[3][0], []).append(e[0] / tps * 1000)
    end = [e[0] / tps * 1000 for e in events if e[1] == "meta" and e[2] == 0x2F]
    print("note-ons (ms):", " ".join(f"{t:.1f}" for t, _ in on))
    gaps = [on[i + 1][0] - on[i][0] for i in range(len(on) - 1)]
    print("gaps (ms):    ", " ".join(f"{g:.1f}" for g in gaps))
    lengths = []
    for t, note in on:
        later = [x for x in off.get(note, []) if x >= t]
        if later:
            lengths.append(min(later) - t)
            off[note].remove(min(later))
    print("lengths (ms): ", " ".join(f"{l:.1f}" for l in lengths))
    print("end of track (ms):", " ".join(f"{t:.1f}" for t in end))

    worst_gap = max((abs(g - a.gap_ms) for g in gaps), default=0.0)
    # The last note may be held at Stop; it is not compared with --length-ms.
    worst_len = max((abs(l - a.length_ms) for l in lengths[:len(on) - 1]), default=0.0)
    print(f"largest error: gap {worst_gap:.2f} ms, length {worst_len:.2f} ms (tolerance {a.tolerance_ms:g} ms)")
    sys.exit(0 if max(worst_gap, worst_len) <= a.tolerance_ms else 1)


if __name__ == "__main__":
    main()
