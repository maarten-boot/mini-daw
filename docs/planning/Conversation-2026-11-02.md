# Conversation log — 2026-11-02

Continues `Conversation-2026-11-01.md` (last section 29: Windows CI failure in T-REC-07, not fixed yet).

## 1. Linux build does not start on Ubuntu 24.04
> when starting on ubu24: ./mini-daw: /lib/x86_64-linux-gnu/libm.so.6: version `GLIBC_2.43' not found (required by ./mini-daw)

- Expected for `dist/linux/mini-daw`: it is built on this VM (glibc 2.43); Ubuntu 24.04 has glibc 2.39 (see `CLAUDE.md`, `dist/`).
- Downloaded the CI build of commit `aacc2ee` (run 41357305545, linux job passed) to `dist/linux-ubuntu24/mini-daw`; it needs at most GLIBC_2.38.
- Idea for later: stop putting the local build in `dist/linux/`, or name it after the glibc it needs, to avoid this mix-up.

## 2. Windows test failure fixed
> yes fix the windows test failure

- CI log of run 41357305545 confirmed it: both T-REC-07 tests fail at `readWav` while the writer still has the file open. No MSVC warnings.
- Cause: `_wfopen_s` (introduced in crosscheck #3 to avoid MSVC's C4996 on `_wfopen`) opens the file without sharing, so nobody else can read a take while it is being recorded. Wine does not enforce this, so the Wine run passed.
- Fix: `_wfsopen(path, mode, _SH_DENYWR)` — others may read, not write. The tests now show the error text on failure.
- Linux 65/65; MinGW under Wine 64/64 (Wine cannot show the problem, so CI on real Windows is the check).
