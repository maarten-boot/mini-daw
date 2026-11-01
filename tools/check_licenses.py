#!/usr/bin/env python3
"""Licence checks for mini-daw.

T-LIC-02: every dependency declared in cmake/Dependencies.cmake has a row in
          THIRD_PARTY.md with the same version and an allowed licence.
T-LIC-03: the repository contains no ASIO SDK files.
Also checks that every GitHub Actions step uses an action pinned to a full
commit SHA.

Exit code 0 = all checks pass, 1 = at least one failure.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ALLOWED_LICENCES = {"MIT", "BSD-2-Clause", "BSD-3-Clause", "Zlib", "BSL-1.0", "Apache-2.0"}

# File names that only appear in the Steinberg ASIO SDK.
ASIO_SDK_FILES = {"asio.h", "asio.cpp", "asiodrivers.h", "asiodrivers.cpp", "asiosys.h",
                  "asiolist.h", "asiolist.cpp", "iasiodrv.h", "ginclude.h"}


def parse_dependencies(cmake_file: Path) -> dict[str, str]:
    text = cmake_file.read_text(encoding="utf-8")
    deps = {}
    for m in re.finditer(r"FetchContent_Declare\(\s*(\w+)(.*?)\)", text, re.S):
        tag = re.search(r"GIT_TAG\s+(\S+)", m.group(2))
        deps[m.group(1).lower()] = tag.group(1) if tag else ""
    return deps


def parse_third_party(md_file: Path) -> dict[str, tuple[str, str]]:
    """Read the first table only; later sections (e.g. "## Planned") are informational."""
    rows = {}
    for line in md_file.read_text(encoding="utf-8").splitlines():
        if line.startswith("## ") and rows:
            break
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 3 or not line.lstrip().startswith("|"):
            continue
        name, version, licence = (c.strip("` ") for c in cells[:3])
        if name.lower() in {"name", ""} or set(name) <= {"-", ":"}:
            continue
        rows[name.lower()] = (version, licence)
    return rows


def tracked_files(root: Path) -> list[Path]:
    try:
        out = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             cwd=root, capture_output=True, check=True)
        return [root / p for p in out.stdout.decode().split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        skip = {".git", "_deps"}
        return [p for p in root.rglob("*")
                if p.is_file() and not any(part in skip or part.startswith("build")
                                           for part in p.relative_to(root).parts)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args()
    root: Path = args.root.resolve()
    errors: list[str] = []

    # T-LIC-02 ------------------------------------------------------------
    deps = parse_dependencies(root / "cmake" / "Dependencies.cmake")
    third = parse_third_party(root / "THIRD_PARTY.md")
    if not deps:
        errors.append("no FetchContent_Declare entries found in cmake/Dependencies.cmake")
    for name, tag in sorted(deps.items()):
        if name not in third:
            errors.append(f"{name}: missing from THIRD_PARTY.md")
            continue
        version, licence = third[name]
        if version != tag:
            errors.append(f"{name}: THIRD_PARTY.md says version {version!r}, CMake pins {tag!r}")
        if licence not in ALLOWED_LICENCES:
            errors.append(f"{name}: licence {licence!r} is not in the allowed list {sorted(ALLOWED_LICENCES)}")
    for name in sorted(set(third) - set(deps)):
        errors.append(f"{name}: listed in THIRD_PARTY.md but not declared in CMake")

    # T-LIC-03 ------------------------------------------------------------
    for path in tracked_files(root):
        if path.name.lower() in ASIO_SDK_FILES:
            errors.append(f"ASIO SDK file in repository: {path.relative_to(root)}")

    # Pinned GitHub Actions -------------------------------------------------
    for wf in sorted((root / ".github" / "workflows").glob("*.y*ml")):
        for n, line in enumerate(wf.read_text(encoding="utf-8").splitlines(), 1):
            m = re.match(r"\s*-?\s*uses:\s*([^\s#]+)", line)
            if m and not m.group(1).startswith("./"):
                ref = m.group(1).partition("@")[2]
                if not re.fullmatch(r"[0-9a-f]{40}", ref):
                    errors.append(f"{wf.relative_to(root)}:{n}: action not pinned to a commit SHA: {m.group(1)}")

    for e in errors:
        print(f"FAIL: {e}")
    if errors:
        return 1
    print(f"OK: {len(deps)} dependencies checked, no ASIO SDK files, all actions pinned")
    return 0


if __name__ == "__main__":
    sys.exit(main())
