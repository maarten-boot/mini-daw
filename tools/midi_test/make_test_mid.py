#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write a MIDI file with evenly spaced test notes (for T-REC-01 / T-REC-04).

Play it into a MIDI input while mini-daw records, then check the recorded
take with check_take_mid.py. On Linux, for example:

    tools/midi_test/make_test_mid.py /tmp/send.mid
    mini-daw --record-test 8 /tmp/takes &   # records every MIDI input
    sleep 1.5; aplaymidi -p 14:0 /tmp/send.mid   # 14:0 = "Midi Through"

Options: --notes N (10), --gap-ms (500), --length-ms (100), --hold
(the last note gets no note-off, to test notes held at Stop).
"""
import argparse
import struct


def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("output")
    ap.add_argument("--notes", type=int, default=10)
    ap.add_argument("--gap-ms", type=float, default=500.0)
    ap.add_argument("--length-ms", type=float, default=100.0)
    ap.add_argument("--hold", action="store_true")
    a = ap.parse_args()

    ppq, tempo_us = 960, 500000  # 120 BPM: 1920 ticks per second
    ticks = lambda ms: round(ms * 1.92)
    events = []
    for i in range(a.notes):
        t = ticks(i * a.gap_ms)
        note = 60 + i % 24
        events.append((t, bytes([0x90, note, 100])))
        if not (a.hold and i == a.notes - 1):
            events.append((t + ticks(a.length_ms), bytes([0x80, note, 0])))
    events.append((ticks(a.notes * a.gap_ms), bytes([0xB0, 64, 127])))  # one controller

    track = vlq(0) + b"\xff\x51\x03" + tempo_us.to_bytes(3, "big")
    last = 0
    for t, data in sorted(events, key=lambda e: e[0]):
        track += vlq(t - last) + data
        last = t
    track += vlq(0) + b"\xff\x2f\x00"
    with open(a.output, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, ppq))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)
    print(f"{a.output}: {a.notes} notes, {a.gap_ms:g} ms apart, {a.length_ms:g} ms long")


if __name__ == "__main__":
    main()
