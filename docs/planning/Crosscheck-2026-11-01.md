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
