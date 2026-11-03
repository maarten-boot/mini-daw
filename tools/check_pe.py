#!/usr/bin/env python3
"""Checks the Windows programs of mini-daw (P9b, T-PKG-01).

    python3 tools/check_pe.py <folder with mini-daw.exe and mini-daw.com>

mini-daw.exe must be a window program (PE subsystem 2, no console),
mini-daw.com a console program (subsystem 3), and both must carry the icon
resource GLFW_ICON and version information. Exit code 1 if not.
Part of mini-daw, MIT licence.
"""
import struct
import sys
from pathlib import Path

SUBSYSTEMS = {2: "window (GUI)", 3: "console"}


def read_pe(path: Path):
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE header")
    coff = pe + 4
    nsections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    subsystem = struct.unpack_from("<H", data, opt + 68)[0]
    dirs = opt + (112 if magic == 0x20B else 96)
    rsrc_rva, rsrc_size = struct.unpack_from("<II", data, dirs + 2 * 8)  # entry 2: resources
    sections = []
    for i in range(nsections):
        s = opt + opt_size + i * 40
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, max(vsize, rawsize), rawptr))

    def off(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError("RVA outside the sections")

    names, types = set(), set()
    if rsrc_rva:
        base = off(rsrc_rva)

        def entries(dir_off):
            named, ids = struct.unpack_from("<HH", data, dir_off + 12)
            for i in range(named + ids):
                name, target = struct.unpack_from("<II", data, dir_off + 16 + 8 * i)
                yield name, target

        def name_of(name):
            if name & 0x80000000:
                o = base + (name & 0x7FFFFFFF)
                n = struct.unpack_from("<H", data, o)[0]
                return data[o + 2:o + 2 + 2 * n].decode("utf-16-le")
            return name

        for tname, ttarget in entries(base):
            t = name_of(tname)
            types.add(t)
            if t == 14 and ttarget & 0x80000000:  # RT_GROUP_ICON: the icon names
                for iname, _ in entries(base + (ttarget & 0x7FFFFFFF)):
                    names.add(name_of(iname))
    return subsystem, types, names


def main() -> int:
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(".")
    want = {"mini-daw.exe": 2, "mini-daw.com": 3}
    ok = True
    for file, subsystem in want.items():
        path = folder / file
        try:
            got, types, icons = read_pe(path)
        except (OSError, ValueError) as e:
            print(f"FAIL {file}: {e}")
            ok = False
            continue
        problems = []
        if got != subsystem:
            problems.append(f"subsystem {SUBSYSTEMS.get(got, got)}, expected {SUBSYSTEMS[subsystem]}")
        if "GLFW_ICON" not in icons:
            problems.append("no GLFW_ICON icon resource")
        if 16 not in types:
            problems.append("no version information")
        print(("FAIL " if problems else "OK   ") + f"{file}: {SUBSYSTEMS.get(got, got)}, icon {sorted(map(str, icons))}"
              + ("" if not problems else " - " + "; ".join(problems)))
        ok = ok and not problems
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
