configure_file(core/include/minidaw/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/minidaw/version.hpp @ONLY)

# Core library: everything that is not GUI. Tests link against this.
add_library(minidaw_core STATIC
    core/src/alsa_quiet.cpp
    core/src/audio_stream.cpp
    core/src/color.cpp
    core/src/config.cpp
    core/src/devices.cpp
    core/src/log.cpp
    core/src/meter.cpp
    core/src/monitor.cpp
    core/src/recorder.cpp
    core/src/midi_input.cpp
    core/src/recording_session.cpp
    core/src/smf.cpp
    core/src/track.cpp
    core/src/theme.cpp
    core/src/wav.cpp)
target_include_directories(minidaw_core PUBLIC
    core/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(minidaw_core
    PUBLIC  minidaw_rtaudio minidaw_rtmidi
    PRIVATE minidaw_warnings)
if(WIN32)
    target_link_libraries(minidaw_core PRIVATE shell32 ole32)  # SHGetKnownFolderPath
endif()

if(MINIDAW_BUILD_APP)
    # Console subsystem on Windows for now, so --list-devices output is visible.
    add_executable(minidaw app/main.cpp app/imgui_theme.cpp)
    if(WIN32)
        target_sources(minidaw PRIVATE app/renderer_d3d11.cpp)
    else()
        target_sources(minidaw PRIVATE app/renderer_opengl.cpp)
    endif()
    set_target_properties(minidaw PROPERTIES OUTPUT_NAME "mini-daw")
    if(WIN32)
        target_link_libraries(minidaw PRIVATE shell32)  # ShellExecuteW: View > Log > Open log folder
    endif()
    target_link_libraries(minidaw PRIVATE minidaw_core minidaw_imgui minidaw_warnings)
endif()
