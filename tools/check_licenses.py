#!/usr/bin/env python3
"""Licence checks for mini-daw.

T-LIC-01: LICENSE is the MIT licence for Maarten Boot.
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
        # "GIT_TAG <40-hex commit>  # <release>": pinned by hash, the release in the comment.
        tag = re.search(r"GIT_TAG\s+([0-9a-f]{40})\)?[ \t]*#[ \t]*(\S+)", m.group(2) + text[m.end():m.end() + 80])
        deps[m.group(1).lower()] = tag.group(2) if tag else ""
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

    # T-LIC-01 ------------------------------------------------------------
    lic = root / "LICENSE"
    if not lic.is_file():
        errors.append("LICENSE file missing at repository root")
    else:
        text = lic.read_text(encoding="utf-8")
        if not text.startswith("MIT License") or "Copyright (c) 2026 Maarten Boot" not in text:
            errors.append("LICENSE is not the expected MIT text with 'Copyright (c) 2026 Maarten Boot'")

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
        if not tag:
            errors.append(f"{name}: not pinned to a commit hash with its release as a comment "
                          "(GIT_TAG <40-hex sha>  # <release>)")
            continue
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

    # VST trademark (SPEC-LIC-05, T-LIC-06) ------------------------------------
    sentence = "VST is a registered trademark of Steinberg Media Technologies GmbH."
    for doc in ("README.md", "THIRD_PARTY.md", "src/app/main.cpp"):
        p = root / doc
        if not p.exists() or sentence not in p.read_text(encoding="utf-8"):
            errors.append(f"{doc}: the VST trademark sentence is missing ({sentence!r})")
    readme = (root / "README.md").read_text(encoding="utf-8") if (root / "README.md").exists() else ""
    first = re.search(r"VST", readme)
    if first and readme[first.end():first.end() + 1] != "\u00ae":
        errors.append("README.md: the first use of VST must be written VST\u00ae")
    if re.search(r"\bVSTi\b", readme):
        errors.append("README.md: no derived words such as 'VSTi' (Steinberg's VST Usage Guidelines)")

    for e in errors:
        print(f"FAIL: {e}")
    if errors:
        return 1
    print(f"OK: LICENSE is MIT, {len(deps)} dependencies checked, no ASIO SDK files, all actions pinned, "
          "VST trademark text present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
