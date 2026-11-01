# mini-daw

A small program to record MIDI and audio input on several tracks at once, with monitoring and playback. VST3 instrument hosting comes later. Windows 10 first; Linux and macOS later.

**Status:** early skeleton (step P2). The program lists the audio devices and MIDI ports it finds. Recording is not implemented yet. See `docs/planning/` for the Spec, Plan and test cases.

## Building

You need CMake 3.24 or newer, a C++20 compiler and Git. Dependencies are downloaded automatically at configure time (see `THIRD_PARTY.md`).

### Windows (Visual Studio 2022)

```
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\src\Release\mini-daw.exe
```

### Linux

Install the development packages first, for example on Debian/Ubuntu:

```
sudo apt install build-essential cmake ninja-build libasound2-dev libpulse-dev xorg-dev libgl-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/mini-daw
```

### Cross-compiling for Windows from Linux (MinGW-w64)

```
cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIDAW_BUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake
cmake --build build-win
```

### Command line

```
mini-daw                 open the GUI
mini-daw --list-devices  print audio devices and MIDI ports, then exit
mini-daw --version       print the version, then exit
```

## Licence

mini-daw is released under the MIT licence (see `LICENSE`): anyone may use and modify it.

### About ASIO

ASIO support is **optional and off by default**, and is planned for a later step. The Steinberg ASIO SDK is never included in this repository. Steinberg offers it under GPLv3 or under its own proprietary licence (https://www.steinberg.net/developers/asiosdk-open/).

- The mini-daw source code stays MIT in all cases.
- If you build with `-DMINIDAW_WITH_ASIO=ON` and use the ASIO SDK under GPLv3, the resulting **binary is GPLv3** and must be distributed under GPLv3 terms, including its source code.
- ASIO is a trademark of Steinberg Media Technologies GmbH. It is not part of the mini-daw name.
