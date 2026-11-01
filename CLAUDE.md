# mini-daw — notes for Claude Code sessions

Small MIDI/audio recorder for Windows 10 (Linux/macOS later), C++20, MIT licence.
Planning documents live in `docs/planning/`: read `Spec.md`, `Plan.md`, `Tests.md` and the latest `Conversation-*.md` (its last section is the handover) before starting work.

## Working rules for this project
- Keep the running log `docs/planning/Conversation-YYYY-MM-DD.md` (one file per day), and keep `Spec.md`, `Plan.md` and `Tests.md` up to date. Every test traces to a Spec item; record results in the Results table of `Tests.md`.
- Suggest a Spec ↔ Plan ↔ code crosscheck after significant changes; record it in `docs/planning/Crosscheck-*.md`.
- If a user sentence contains errors or uncertainties, suggest corrections and confirm before acting on anything that depends on them.
- GUI colours: white background, black text, every foreground/background pair ≥ 4.5:1. New UI colours must use a `minidaw::Role` (see `src/core/include/minidaw/theme.hpp`); add new fg/bg combinations to `colorPairs()` so the tests check them.
- GitHub Actions: pin every action to a full commit SHA with the tag in a comment; use the latest working release; explain any exception in Spec §7. `tools/check_licenses.py` enforces this.
- Dependencies must have MIT-compatible licences and be listed in `THIRD_PARTY.md` with the same version as `cmake/Dependencies.cmake`. Never add ASIO SDK files to the repository.
- Audio callback (`AudioStream::audioCallback`): no locks, no allocation, no I/O.

## Layout
- `src/core/` — library without GUI: colours/contrast (`color`, `theme`), config file (`config`), device scan (`devices`), audio stream (`audio_stream`).
- `src/app/` — GUI: `main.cpp`, `imgui_theme.cpp` (palette → ImGui), `renderer_d3d11.cpp` (Windows), `renderer_opengl.cpp` (Linux/macOS).
- `tests/` — Catch2 tests; test names start with their Tests.md ID where there is one (e.g. `T-UI-02: ...`).
- `tools/check_licenses.py` — licence, ASIO-file and action-pinning check (also a ctest).
- `dist/` — not committed; test builds for the user to try (`dist/windows/`, `dist/linux/`). The local Linux build needs glibc 2.43; the CI download (built on Ubuntu 24.04, glibc 2.39) runs on more systems.

## Build and test
Linux (keep build folders out of the repository, e.g. in the session scratchpad):
```
cmake -S . -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build <build> && ctest --test-dir <build> --output-on-failure
```
Windows cross-build from Linux (MinGW-w64 13 is installed):
```
cmake -S . -B <build-win> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake
cmake --build <build-win>
cp <build-win>/src/mini-daw.exe dist/windows/
```
Windows/MSVC is built by GitHub Actions (`.github/workflows/build.yml`).

## How things were verified on this machine (Linux VM)
- **GUI screenshots:** `Xvfb :77 -screen 0 1150x750x24 &`, then `DISPLAY=:77 LIBGL_ALWAYS_SOFTWARE=1 mini-daw --config <file>`, capture with `import -window root shot.png`, drive with `xdotool` (key presses, menu clicks). Always pass `--config` with a scratch file so the user's real config is not touched.
- **Windows exe under Wine:** the root disk is nearly full (≈1 GB free) and a Wine prefix needs ≈1.2 GB, so put it in RAM: `mkdir -p /dev/shm/minidaw-wine && WINEPREFIX=/dev/shm/minidaw-wine WINEDEBUG=-all wineboot -i`, run, then `wineserver -k; rm -rf /dev/shm/minidaw-wine`. (Wine refuses to create the prefix unless the folder exists first.)
- **Wine limits:** D3D11 works (hardware and `MINIDAW_D3D_WARP=1`), unit tests pass, but RtAudio's WASAPI client fails to initialise under Wine. Audio on Windows must be tested by the user on the real Windows 10 machine.
- This VM has no sound card (ALSA→PulseAudio only) and no MIDI access (`/dev/snd/seq` permission denied).

## Useful program options
`--config <file>`, `--list-devices`, `--audio-test <seconds>`, `--contrast-report`, `--version`. Environment: `MINIDAW_D3D_WARP=1` forces the Windows software renderer.
