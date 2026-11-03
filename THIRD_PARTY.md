# Third-party components

mini-daw itself is MIT-licensed (see `LICENSE`). It uses the components below, which are all fetched at build time at the pinned versions. None of them is stored in this repository.

`tools/check_licenses.py` checks that this table matches `cmake/Dependencies.cmake` and that every licence is on the allowed list (MIT, BSD-2/3-Clause, Zlib, BSL-1.0, Apache-2.0).

| Name | Version | Licence | Source | Used for |
|------|---------|---------|--------|----------|
| rtaudio | 6.0.1 | MIT | https://github.com/thestk/rtaudio | Audio input/output (WASAPI on Windows) |
| rtmidi | 6.0.0 | MIT | https://github.com/thestk/rtmidi | MIDI input/output |
| imgui | v1.92.9 | MIT | https://github.com/ocornut/imgui | GUI |
| glfw | 3.4 | Zlib | https://github.com/glfw/glfw | Window, OpenGL context, input |
| nlohmann_json | v3.12.0 | MIT | https://github.com/nlohmann/json | Project files (JSON), header-only |
| catch2 | v3.16.1 | BSL-1.0 | https://github.com/catchorg/Catch2 | Unit tests (not part of the program) |

RtAudio and RtMidi use the MIT licence text with one extra non-binding request: that changes be sent back to the author.

## Build tools

Used to build or package mini-daw, not part of the program itself.

| Tool | Licence | Used for | Notes |
|------|---------|----------|-------|
| NSIS 3 | zlib/libpng (the installer stub that becomes part of the setup program) | Windows installer (P9b) | From the CI runner image or Chocolatey; `makensis` also runs on Linux |
| Pillow | MIT-CMU (HPND) | Drawing the icon once (`tools/icon/make_icon.py`) | Only on the developer's machine; the generated files are committed |

## Planned

| Component | Licence | Step | Notes |
|-----------|---------|------|-------|
| Steinberg VST3 SDK | MIT (since SDK 3.8; LICENSE file checked 2026-11-01) | P10 | VST3 instrument hosting |
| Steinberg ASIO SDK | GPLv3 or Steinberg proprietary | P11b | **Optional, off by default, never in this repository.** A binary built with it under GPLv3 is itself GPLv3. See README. |
