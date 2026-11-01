# Crosscheck Spec ↔ Plan ↔ Tests ↔ code — 2026-11-01 (after P2)

Scope: Spec v0.5, Plan, Tests, and the code on branch `p2-skeleton` (commit `203d9d4`).

## Result per Spec area
| Area | Code state | Verdict |
|------|-----------|---------|
| TECH-01..03, 05, 06 | Implemented in CMake/FetchContent | ✅ matches |
| TECH-04 (VST3) | Not started | ✅ as planned (P10). Plan P2 row wrongly listed it → **F2** |
| DEV-01 | `scanDevices()` lists audio devices, MIDI inputs **and outputs** | ⚠️ code does more than Spec says → **F1** |
| DEV-02, 03 | Not started | ✅ as planned (P4) |
| UI-01 | White background, black text in `applyBaseTheme()` | ✅ |
| UI-02 | Other ImGui colours still come from `StyleColorsLight()`; e.g. disabled text is about 2.8:1 | ❌ known gap, fixed in P3 → **F3** |
| UI-03..07 | Not started | ✅ as planned (P3, P7) |
| REC, MON, PLAY, VST, PRJ, NF | Not started | ✅ as planned |
| LIC-01..04 | `LICENSE`, `THIRD_PARTY.md`, check script, ASIO option | ✅. T-LIC-01 is marked automated but has no automated check → **F4** |
| CI-01..03 | Workflow pinned, toolchain file, check script | ✅. CI not run (no remote, by design for now) |

## Findings and actions
| # | Finding | Action |
|---|---------|--------|
| F1 | SPEC-DEV-01 lists only MIDI **input** ports; the code also lists MIDI output ports, which SPEC-PLAY-03 needs | Spec DEV-01 extended to include MIDI output ports |
| F2 | Plan P2 row says TECH-01..06, but TECH-04 (VST3) belongs to P10 | Plan P2 row corrected to TECH-01..03, 05, 06 |
| F3 | UI-02 not met yet: ImGui default light colours below 4.5:1 | Fixed in P3 (planned) |
| F4 | T-LIC-01 is marked automated but nothing checks it | Added a `LICENSE` check to `tools/check_licenses.py` |
| F5 | SPEC-NF-02 (latency) has no test case | Added T-NF-02 |
| F6 | GLFW is built without Wayland on Linux; not in the Spec | Noted under SPEC-TECH-05 (Linux uses X11/XWayland for now) |
| F7 | Windows build uses the console subsystem, so a console window opens next to the GUI; not in Spec or Plan | Added Plan step P9b (Windows packaging: GUI subsystem, icon, installer) |
| F8 | Plan P12 says "Port to Linux", but Linux already builds and runs in CI | P12 reworded: full Linux/macOS support and testing |
| F9 | `imgui_demo.cpp` is compiled but never used | Removed from the build |

No contradictions between Spec and Plan were found beyond F2 and F8.

---

# Crosscheck #2 — 2026-11-01 (after the Windows renderer change and P4)

Scope: Spec v0.8, Plan, Tests, code at commit `747687c`.

| Area | Verdict |
|------|---------|
| TECH-05 renderer (D3D11 + WARP / OpenGL) | ✅ code matches Spec; tested on real Windows (hardware path) |
| DEV-01..06 | ✅ implemented; DEV-02/03 and T-DEV-04 (there is no SPEC-DEV-04) tested on Linux and by unit tests; real-Windows audio (T-DEV-07) still open |
| UI-01..08 | ✅; UI-07 partly (levels as text; real meters in P7) |
| NF-01 (no locks/allocation in the audio callback) | ✅ callback uses only atomics and `memset` |
| NF-02 (latency) | ⚠️ cannot be met with RtAudio's shared-mode WASAPI; open issue in Spec §8 |
| LIC, CI | ✅ licence/pinning check passes |
| Spec items without tests | TECH-01..06 are design rules (checked by the build and by T-LIC-02); the "a" items (DEV-02a, DEV-03a, UI-02a/03a/04a) are covered by the tests of their parent items. No gap. |

## Findings
| # | Finding | Action |
|---|---------|--------|
| G1 | **Resolved 2026-11-01:** MSVC build passes in CI (run 41337681818); the two C4996 warnings were fixed. Original note: The Windows build has only been compiled with MinGW. The Visual Studio (MSVC) build used by GitHub CI has never run; `std::getenv` will give MSVC warnings (C4996), and other MSVC-only problems are possible | First push to GitHub runs CI; fix whatever it reports |
| G2 | `scanDevices()` temporarily redirects `std::cerr` (to capture RtMidi messages). Harmless today (only the GUI thread writes to `std::cerr`), but not thread-safe in general | Noted in the code; revisit if other threads start logging |
| G3 | PulseAudio on Linux reports 32 input channels, so 32 level lines appear. Not a Windows issue | Revisit with the meters in P7 (e.g. only armed inputs) |
| G4 | Plan still lists P9b, P11b, P12 correctly; nothing outdated | — |

No contradictions between Spec, Plan and code.

---

# Crosscheck #3 — 2026-11-01 (after P5)

Scope: Spec v0.9, Plan, Tests, README, code at commit `b010e39` (P5 recording engine + MSVC C4324 fix).

| Area | Verdict |
|------|---------|
| REC-03 (24-bit WAV) | ✅ `WavWriter`, T-REC-02 |
| REC-05 (no data lost, disk I/O off the audio thread) | ✅ ring per take + disk thread; T-REC-06/08 |
| REC-05a (header refreshed each second) | ⚠️ refreshed and flushed, but only to the OS cache; Spec also promises power loss → **H2** |
| REC-05b (lost audio reported) | ⚠️ counted when the ring is full; frames lost on a disk write error are not counted → **H3**. "About 4 s" is really ≥ 4 s → **H6** |
| REC-05c (4 GiB limit) | ⚠️ implemented (limit is exact incl. padding byte), but no test → **H4** |
| REC-06 (`--record-test`) | ✅ behaviour; output wording differs from Spec → **H5** |
| REC-02 (sample-synchronised takes) | ⚠️ all takes start in the same block ✅, but a dropped block shifts the rest of that take only → **H1** (matters from P6) |
| NF-01 | ✅ `Recorder::process` uses only atomics and plain copies; `stop()` waits for the audio thread to leave (seq_cst flag pair is correct) |
| DEV, UI, LIC, CI | ✅ unchanged since crosscheck #2; CI green (run 41354630677) |
| Docs | ❌ several outdated statements → **H7**, **H8** |

## Findings
| # | Finding | Proposed action |
|---|---------|-----------------|
| H1 | When a take's ring is full, `process()` drops that block **for that take only** and the gap is closed up in the file. Everything after it in that take is earlier than in the other takes (and later, the MIDI timeline). The loss is reported, but the timeline is silently wrong. | Keep the timeline: count the gap, and let the disk thread write that many frames of silence at the right place (gap marker in the ring, or a small SPSC queue of gap positions). Loss is still reported. Do it before multi-track recording in P6. |
| H2 | SPEC-REC-05a promises a valid file after "a crash or power loss". `updateHeader()` calls `ofstream::flush()`, which only reaches the OS cache: enough for a program crash, not for power loss. | Add an OS sync (`FlushFileBuffers` / `fsync`) at each header refresh (once a second, cheap) — or reword the Spec to "program crash". Recommended: add the sync. |
| H3 | On a disk write error, the chunk already read from the ring (up to 16 384 samples) is discarded without being counted as lost. The stream is then in a fail state, so `close()` cannot rewrite the header; the file is valid only up to the last refresh. | Count those frames as lost; `clear()` the stream state and try a final header rewrite. |
| H4 | SPEC-REC-05c (4 GiB limit) has no test case. | Add T-REC-11 (A): writer started just below the limit through a test hook; the write that would cross it fails with the "4 GiB" message, the file stays valid. |
| H5 | SPEC-REC-06 and T-REC-10 say "lost frames" and "dropouts"; the program prints "dropped" and "xruns". The exit code (1 when data was lost) is not in the Spec. | Print "lost frames" and "dropouts (xruns)"; add the exit code to SPEC-REC-06. |
| H6 | SPEC-REC-05b says "about 4 seconds per take"; the ring rounds up to a power of two (≈ 5.5 s at 48 kHz, up to 8 s). | Reword to "at least 4 seconds". |
| H7 | Outdated text: README status ("steps P2–P4 … Recording is not implemented yet"); Plan P2 row ("CI not run yet: no GitHub remote"); Plan PX row (only #1 and #2); Spec §9 GitHub question (remote now exists: `maarten-boot/mini-daw`); Spec §8b "P4/P5 must handle this" (P4 warns on a rate mismatch; WASAPI shared mode converts); crosscheck #2 mentions "DEV-04", which is not a Spec item. | Update the text. |
| H8 | Tests.md: T-UI-10/11 follow a blank line without a table header, so they do not render as a table; T-DEV-05 sits in the CI section. | Move them into the UI and Devices tables. |
| H9 | `Recorder::droppedFrames()` is documented as "safe from the GUI thread", but it walks `takes_`, which `start()`/`stop()` replace. Fine today (same thread), unsafe if another thread stops a recording. | Make the comment precise now; revisit when the GUI polls during recording (P6). |

Build and tests at `b010e39` (fresh Linux build, Release): 58/58 ctest pass.

No contradictions between Spec and Plan; the gaps are in REC-05a/b/c detail, REC-02 robustness, and stale text.

## Actions taken (user: "yes to both, fix H1–H9"; H2 as a config option, default on)
| # | Done |
|---|------|
| H1 | Audio thread passes a gap marker (lock-free ring of 256 per take) before the next block after a loss; the disk thread writes that many frames of silence. A loss still open at stop is written as silence too, so all takes have the same length. Spec REC-02 extended. T-REC-09 updated, T-REC-13 added. |
| H2 | `WavWriter` now uses `FILE*`; header refresh can force the file to disk (`_commit` = `FlushFileBuffers` on Windows, `fsync` elsewhere). Option `sync_to_disk` in `[recording]`, default `true`; always synced on close. Spec REC-05a, T-REC-15, T-REC-07 extended. |
| H3 | Frames read from the ring but not written are counted; result's lost frames = captured − real audio in the file (covers every cause). After a failed write the file is cut back (`resize_file`) and gets a valid header. T-REC-12, T-REC-14. |
| H4 | T-REC-11 (test hook `pretendFramesWrittenForTest`). |
| H5 | `--record-test` and `--audio-test` print "lost frames" and "dropouts (xruns)"; exit code and the 1–3600 s range in SPEC-REC-06. |
| H6 | Spec REC-05b: "at least 4 seconds (≈ 5.5 s at 48 kHz)". |
| H7 | README status and recording section, Plan P2/P5/PX rows, Spec §8b and §9, crosscheck #2 DEV-04 note updated. |
| H8 | T-UI-10/11 moved into the UI table, T-DEV-05 into the Devices table. |
| H9 | Comment in `recorder.hpp` now says which thread may call the progress functions. |

Result: Linux 65/65 ctest; MinGW build under Wine 64/64 Catch2 cases; live `--record-test` on Linux with sync on and off: 0 lost, 0 dropouts.
