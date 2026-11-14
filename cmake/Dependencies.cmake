# Third-party dependencies, fetched at configure time at pinned versions.
# Every FetchContent_Declare below MUST have a matching row in THIRD_PARTY.md
# with the same name and version (checked by tools/check_licenses.py, T-LIC-02).

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---------------------------------------------------------------------------
# Declarations (name / tag are parsed by tools/check_licenses.py).
# Each is pinned to the full commit hash of its release (a tag can be moved,
# a hash cannot), with the release as a comment: the version THIRD_PARTY.md
# must list. No GIT_SHALLOW: a shallow clone needs a branch or tag name.
# CI keeps the fetched sources in its cache, keyed on this file.
# ---------------------------------------------------------------------------
FetchContent_Declare(rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG        b4f04903312e0e0efffbe77655172e0f060dc085  # 6.0.1
    SOURCE_SUBDIR  _not_a_cmake_project)   # we compile the sources ourselves

FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        7e31b5bfcc9350058fbaf81d62a8eb39dd29c5f7  # 6.1.0
    SOURCE_SUBDIR  _not_a_cmake_project)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        c1aaac65e896a45f81f47a4d68f9959e0f86c859  # v1.93.0
    SOURCE_SUBDIR  _not_a_cmake_project)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        d9d6f0f1f967807ffade6598ea9a631ebaf37a56)  # 3.5.1

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        55f93686c01528224f448c19128836e7df245f72)  # v3.12.0

# VST3 SDK (P10): only the parts a host needs; vstgui4 and doc are not fetched.
FetchContent_Declare(vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96  # v3.8.1_build_84
    GIT_SUBMODULES "base;pluginterfaces;public.sdk"
    SOURCE_SUBDIR  _not_a_cmake_project)   # the hosting sources are compiled in src/CMakeLists.txt

# DAWproject export (P27): XML and ZIP.
FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        c8033ce9d039e7f9d134877c363397b3cfe20816  # v1.16
    SOURCE_SUBDIR  _not_a_cmake_project)

FetchContent_Declare(miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        77d0dce8627735138c51770d1799a1ef48f2117d  # 3.1.2
    SOURCE_SUBDIR  _not_a_cmake_project)

FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        08092139210881f01a34e60c5f2fd6afa0ea4024)  # v3.16.1

# ---------------------------------------------------------------------------
# RtAudio — compiled directly, with only the backends we want.
# Windows: WASAPI only (no DirectSound, no ASIO; see SPEC-LIC-03).
# ---------------------------------------------------------------------------
# nlohmann/json — project files (P9). Header-only; its own tests are not built.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
FetchContent_MakeAvailable(vst3sdk)

# ---------------------------------------------------------------------------
# pugixml and miniz (P27, DAWproject files), compiled as our own static
# libraries. miniz's export header is normally made by its CMake; ours is empty.
# ---------------------------------------------------------------------------
FetchContent_MakeAvailable(pugixml miniz)
add_library(minidaw_pugixml STATIC ${pugixml_SOURCE_DIR}/src/pugixml.cpp)
target_include_directories(minidaw_pugixml SYSTEM PUBLIC ${pugixml_SOURCE_DIR}/src)
file(WRITE ${CMAKE_BINARY_DIR}/miniz_export/miniz_export.h
     "#pragma once\n#define MINIZ_EXPORT\n")
add_library(minidaw_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c ${miniz_SOURCE_DIR}/miniz_tdef.c
            ${miniz_SOURCE_DIR}/miniz_tinfl.c ${miniz_SOURCE_DIR}/miniz_zip.c)
target_include_directories(minidaw_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR} ${CMAKE_BINARY_DIR}/miniz_export)
# Large files (WAV takes over 2 GB) through the FILE functions on 32-bit off_t systems.
target_compile_definitions(minidaw_miniz PRIVATE _FILE_OFFSET_BITS=64 _LARGEFILE64_SOURCE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(minidaw_miniz PRIVATE -w)  # not our code
endif()

# ---------------------------------------------------------------------------
# VST3 SDK hosting part (P10), compiled as our own static library: the
# interface IDs, module loading (Windows/Linux), the host helper classes and
# the moduleinfo.json reader. No plugin-side code, no VSTGUI.
# ---------------------------------------------------------------------------
set(vst3 ${vst3sdk_SOURCE_DIR})
set(MINIDAW_VST3_SOURCES
    ${vst3}/pluginterfaces/base/conststringtable.cpp
    ${vst3}/pluginterfaces/base/coreiids.cpp
    ${vst3}/pluginterfaces/base/funknown.cpp
    ${vst3}/pluginterfaces/base/ustring.cpp
    ${vst3}/public.sdk/source/common/commonstringconvert.cpp
    ${vst3}/public.sdk/source/common/memorystream.cpp
    ${vst3}/public.sdk/source/common/commoniids.cpp  # IPlugView, IPlugFrame, content scale (P10d)
    ${vst3}/public.sdk/source/vst/vstinitiids.cpp
    ${vst3}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${vst3}/public.sdk/source/vst/hosting/eventlist.cpp
    ${vst3}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${vst3}/public.sdk/source/vst/hosting/module.cpp
    ${vst3}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${vst3}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${vst3}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${vst3}/public.sdk/source/vst/hosting/processdata.cpp
    ${vst3}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp
    ${vst3}/public.sdk/source/vst/utility/stringconvert.cpp)
if(WIN32)
    list(APPEND MINIDAW_VST3_SOURCES ${vst3}/public.sdk/source/vst/hosting/module_win32.cpp
                                     ${vst3}/public.sdk/source/common/threadchecker_win32.cpp)
elseif(APPLE)
    # macOS later (P12)
else()
    list(APPEND MINIDAW_VST3_SOURCES ${vst3}/public.sdk/source/vst/hosting/module_linux.cpp
                                     ${vst3}/public.sdk/source/common/threadchecker_linux.cpp)
endif()
add_library(minidaw_vst3host STATIC ${MINIDAW_VST3_SOURCES})
target_include_directories(minidaw_vst3host SYSTEM PUBLIC ${vst3})
target_compile_definitions(minidaw_vst3host PUBLIC $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
# The SDK detects C++20 by __cplusplus only; MSVC reports 199711 unless told
# otherwise, so the SDK took its C++17 branch and did not compile (CI run 41629642662).
if(MSVC)
    target_compile_options(minidaw_vst3host PUBLIC /Zc:__cplusplus)
endif()
set_target_properties(minidaw_vst3host PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_link_libraries(minidaw_vst3host PUBLIC ole32 shell32)
else()
    target_link_libraries(minidaw_vst3host PUBLIC ${CMAKE_DL_LIBS})
endif()

FetchContent_MakeAvailable(rtaudio rtmidi)

add_library(minidaw_rtaudio STATIC ${rtaudio_SOURCE_DIR}/RtAudio.cpp)
target_include_directories(minidaw_rtaudio SYSTEM PUBLIC ${rtaudio_SOURCE_DIR})
set_target_properties(minidaw_rtaudio PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(minidaw_rtmidi STATIC ${rtmidi_SOURCE_DIR}/RtMidi.cpp)
target_include_directories(minidaw_rtmidi SYSTEM PUBLIC ${rtmidi_SOURCE_DIR})
set_target_properties(minidaw_rtmidi PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

if(WIN32)
    target_compile_definitions(minidaw_rtaudio PUBLIC __WINDOWS_WASAPI__)
    target_link_libraries(minidaw_rtaudio PUBLIC
        ole32 winmm ksuser mfplat mfuuid wmcodecdspuuid uuid)
    target_compile_definitions(minidaw_rtmidi PUBLIC __WINDOWS_MM__)
    target_link_libraries(minidaw_rtmidi PUBLIC winmm)
elseif(APPLE)
    target_compile_definitions(minidaw_rtaudio PUBLIC __MACOSX_CORE__)
    target_link_libraries(minidaw_rtaudio PUBLIC "-framework CoreAudio" "-framework CoreFoundation")
    target_compile_definitions(minidaw_rtmidi PUBLIC __MACOSX_CORE__)
    target_link_libraries(minidaw_rtmidi PUBLIC
        "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
else()
    find_package(ALSA REQUIRED)
    target_compile_definitions(minidaw_rtaudio PUBLIC __LINUX_ALSA__)
    target_link_libraries(minidaw_rtaudio PUBLIC ALSA::ALSA)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(PULSE IMPORTED_TARGET libpulse-simple libpulse)
        if(PULSE_FOUND)
            target_compile_definitions(minidaw_rtaudio PUBLIC __LINUX_PULSE__)
            target_link_libraries(minidaw_rtaudio PUBLIC PkgConfig::PULSE)
        endif()
    endif()
    target_compile_definitions(minidaw_rtmidi PUBLIC __LINUX_ALSA__)
    target_link_libraries(minidaw_rtmidi PUBLIC ALSA::ALSA)
endif()
target_link_libraries(minidaw_rtaudio PUBLIC Threads::Threads)
target_link_libraries(minidaw_rtmidi PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# GLFW + Dear ImGui (GUI only)
# ---------------------------------------------------------------------------
if(MINIDAW_BUILD_APP)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)  # X11 (and XWayland) for now
    FetchContent_MakeAvailable(glfw imgui)

    # Renderer: Direct3D 11 on Windows (works without an OpenGL driver, falls
    # back to WARP software rendering), OpenGL 3 elsewhere. See SPEC-TECH-05.
    add_library(minidaw_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    target_include_directories(minidaw_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
    target_link_libraries(minidaw_imgui PUBLIC glfw)
    # Item hooks only (no test engine is linked): the hover / pressed / disabled
    # cues of PL1 find the controls through them (src/app/state_cues.cpp).
    target_compile_definitions(minidaw_imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
    if(WIN32)
        target_sources(minidaw_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
        if(MINGW)
            target_link_libraries(minidaw_imgui PUBLIC d3d11 dxgi d3dcompiler_47)
        else()
            target_link_libraries(minidaw_imgui PUBLIC d3d11 dxgi d3dcompiler)
        endif()
    else()
        find_package(OpenGL REQUIRED)
        target_sources(minidaw_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
        target_link_libraries(minidaw_imgui PUBLIC OpenGL::GL)
    endif()
endif()

# ---------------------------------------------------------------------------
# Catch2 (tests only)
# ---------------------------------------------------------------------------
if(MINIDAW_BUILD_TESTS)
    FetchContent_MakeAvailable(catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
