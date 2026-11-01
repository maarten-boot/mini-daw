# Third-party dependencies, fetched at configure time at pinned versions.
# Every FetchContent_Declare below MUST have a matching row in THIRD_PARTY.md
# with the same name and version (checked by tools/check_licenses.py, T-LIC-02).

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

# ---------------------------------------------------------------------------
# Declarations (name / tag are parsed by tools/check_licenses.py)
# ---------------------------------------------------------------------------
FetchContent_Declare(rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG        6.0.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _not_a_cmake_project)   # we compile the sources ourselves

FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _not_a_cmake_project)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _not_a_cmake_project)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

FetchContent_Declare(catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)

# ---------------------------------------------------------------------------
# RtAudio — compiled directly, with only the backends we want.
# Windows: WASAPI only (no DirectSound, no ASIO; see SPEC-LIC-03).
# ---------------------------------------------------------------------------
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
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    target_include_directories(minidaw_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(minidaw_imgui PUBLIC glfw)
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
