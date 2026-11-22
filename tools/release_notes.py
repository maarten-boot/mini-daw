#!/usr/bin/env python3
"""Release checks and notes for mini-daw (P33).

Usage: release_notes.py [--macos] <tag>      e.g. v1.0.0 or v1.0.0-rc1

--macos: the release has the macOS .dmg (P34d; only when the macOS job
built it), so its line is in the downloads.

- The tag must be v<major>.<minor>.<patch>, optionally -rc<n>, and its
  version must equal the VERSION in the top-level CMakeLists.txt.
- Prints the release notes: the "## <version>" section of CHANGELOG.md
  (required for a real release; a -rc tag without one gets a short line),
  followed by the downloads and what each needs.

Exit code 0 = fine, 1 = the tag cannot be released (the reason on stderr).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def cmake_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\s*\(\s*minidaw\s+VERSION\s+(\d+\.\d+\.\d+)", text)
    if not m:
        raise SystemExit("CMakeLists.txt: no 'project(minidaw VERSION x.y.z'")
    return m.group(1)


def changelog_section(version: str) -> str | None:
    path = ROOT / "CHANGELOG.md"
    if not path.exists():
        return None
    lines = path.read_text(encoding="utf-8").splitlines()
    out: list[str] = []
    inside = False
    for line in lines:
        if line.startswith("## "):
            if inside:
                break
            # "## 1.0.0", "## v1.0.0", "## 1.0.0 — 2026-12-01"
            inside = re.match(rf"##\s+v?{re.escape(version)}(\s|$)", line) is not None
            continue
        if inside:
            out.append(line)
    text = "\n".join(out).strip()
    return text or None


DOWNLOADS = """\
## Downloads

| File | For |
|------|-----|
| `mini-daw-{v}-windows-x64-setup.exe` | Windows 10 / 11, 64-bit: the installer (Start menu entry, uninstaller) |
| `mini-daw-{v}-windows-x64.zip` | Windows, without installing: `mini-daw.exe` (window) and `mini-daw.com` (console) |
| `mini-daw-{v}-linux-x64.tar.gz` | Linux x86-64, built on Ubuntu 24.04 (glibc 2.39 or newer); needs ALSA, PulseAudio or PipeWire-Pulse, X11 and OpenGL |
{macos}| `SHA256SUMS.txt` | the checksums of the files above |

The program is not signed: Windows SmartScreen may ask before it runs{macos_note}.
Licence: MIT; the libraries it contains are listed in `THIRD_PARTY.md`.
"""

MACOS_ROW = ("| `mini-daw-{v}-macos.dmg` | macOS 12 or newer, Apple silicon and Intel in one program: a preview, "
             "built and tested automatically but not yet tried on a real Mac. Open it and drag mini-daw to "
             "Applications |\n")
MACOS_NOTE = ("; macOS asks too: the first time, open it from System Settings > Privacy & Security > Open Anyway "
              "(see Installing in the README)")


def main(argv: list[str]) -> int:
    args = argv[1:]
    macos = "--macos" in args
    args = [a for a in args if a != "--macos"]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 1
    tag = args[0]
    m = re.fullmatch(r"v(\d+\.\d+\.\d+)(-rc\d+)?", tag)
    if not m:
        print(f"tag {tag!r} is not v<major>.<minor>.<patch>[-rc<n>]", file=sys.stderr)
        return 1
    version, rc = m.group(1), m.group(2)
    have = cmake_version()
    if have != version:
        print(f"tag {tag} does not match the CMake version {have}: "
              f"set VERSION {version} in CMakeLists.txt or tag v{have}", file=sys.stderr)
        return 1
    notes = changelog_section(version)
    if notes is None:
        if not rc:
            print(f"CHANGELOG.md has no '## {version}' section (needed for a release)", file=sys.stderr)
            return 1
        notes = f"Test build {tag}: not a release."
    full = version + (rc or "")
    print(notes)
    print()
    print(DOWNLOADS.format(v=full, macos=MACOS_ROW.format(v=full) if macos else "",
                           macos_note=MACOS_NOTE if macos else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
