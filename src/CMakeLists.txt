configure_file(core/include/minidaw/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/minidaw/version.hpp @ONLY)

# Core library: everything that is not GUI. Tests link against this.
add_library(minidaw_core STATIC
    core/src/alsa_quiet.cpp
    core/src/audio_stream.cpp
    core/src/color.cpp
    core/src/config.cpp
    core/src/devices.cpp
    core/src/instrument.cpp
    core/src/instrument_rack.cpp
    core/src/log.cpp
    core/src/meter.cpp
    core/src/monitor.cpp
    core/src/player.cpp
    core/src/plugin_scan.cpp
    core/src/project.cpp
    core/src/recorder.cpp
    core/src/midi_input.cpp
    core/src/midi_player.cpp
    core/src/recording_session.cpp
    core/src/smf.cpp
    core/src/track.cpp
    core/src/theme.cpp
    core/src/wav.cpp
    core/src/wav_reader.cpp)
target_include_directories(minidaw_core PUBLIC
    core/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(minidaw_core
    PUBLIC  minidaw_rtaudio minidaw_rtmidi minidaw_vst3host
    PRIVATE minidaw_warnings nlohmann_json::nlohmann_json)
if(WIN32)
    target_link_libraries(minidaw_core PRIVATE shell32 ole32)  # SHGetKnownFolderPath
endif()

if(MINIDAW_BUILD_APP)
    # The program's code, built once and linked into the programs below.
    add_library(minidaw_app OBJECT app/main.cpp app/imgui_theme.cpp)
    if(WIN32)
        target_sources(minidaw_app PRIVATE app/renderer_d3d11.cpp)
    else()
        target_sources(minidaw_app PRIVATE app/renderer_opengl.cpp)
    endif()
    target_link_libraries(minidaw_app PUBLIC minidaw_core minidaw_imgui PRIVATE minidaw_warnings)
    if(WIN32)
        target_link_libraries(minidaw_app PUBLIC shell32)  # ShellExecuteW: View > Log > Open log folder
    endif()

    if(WIN32)
        # Icon and version information (P9b).
        set(MINIDAW_ICON_FILE "${PROJECT_SOURCE_DIR}/res/mini-daw.ico")
        configure_file(app/mini-daw.rc.in ${CMAKE_CURRENT_BINARY_DIR}/mini-daw.rc @ONLY)
        set(MINIDAW_RC ${CMAKE_CURRENT_BINARY_DIR}/mini-daw.rc)

        # mini-daw.exe: no console window (double-click, Start menu).
        add_executable(minidaw WIN32 ${MINIDAW_RC})
        # mini-daw.com: the same program as a console program, for the
        # command-line options; cmd and PowerShell pick .com before .exe, so
        # `mini-daw --list-devices` prints and returns its exit code (P9b).
        add_executable(minidaw_cli ${MINIDAW_RC})
        set_target_properties(minidaw_cli PROPERTIES OUTPUT_NAME "mini-daw" SUFFIX ".com"
                                                     PDB_NAME "mini-daw-cli")  # not the .exe's PDB
        target_link_libraries(minidaw_cli PRIVATE minidaw_app)
        if(MSVC)
            target_link_options(minidaw PRIVATE /ENTRY:mainCRTStartup)  # keep main() in a GUI program
            # Both programs are called mini-daw: no incremental-link file for the
            # .com, so Debug builds do not share mini-daw.ilk (crosscheck #8, O3).
            target_link_options(minidaw_cli PRIVATE /INCREMENTAL:NO)
        endif()

        # Installer (P9b): `cmake --build <build> --target installer` when NSIS is found.
        find_program(MINIDAW_MAKENSIS makensis PATHS "C:/Program Files (x86)/NSIS" "C:/Program Files/NSIS")
        if(MINIDAW_MAKENSIS)
            set(stage ${CMAKE_BINARY_DIR}/installer-stage)
            set(setup ${CMAKE_BINARY_DIR}/mini-daw-${PROJECT_VERSION}-setup.exe)
            # makensis gets native paths (backslashes on Windows, crosscheck #8 O0).
            file(TO_NATIVE_PATH "${stage}" stage_native)
            file(TO_NATIVE_PATH "${setup}" setup_native)
            add_custom_target(installer
                COMMAND ${CMAKE_COMMAND} -E make_directory ${stage}
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:minidaw> $<TARGET_FILE:minidaw_cli> ${stage}/
                COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/LICENSE ${stage}/LICENSE.txt
                COMMAND ${CMAKE_COMMAND} -E copy ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md ${PROJECT_SOURCE_DIR}/README.md
                        ${MINIDAW_ICON_FILE} ${stage}/
                COMMAND ${MINIDAW_MAKENSIS} -V2 -DVERSION=${PROJECT_VERSION} -DSTAGE=${stage_native}
                        -DOUTFILE=${setup_native}
                        ${PROJECT_SOURCE_DIR}/installer/mini-daw.nsi
                DEPENDS minidaw minidaw_cli
                COMMENT "Building ${setup}"
                VERBATIM)
        endif()
    else()
        add_executable(minidaw)
    endif()
    set_target_properties(minidaw PROPERTIES OUTPUT_NAME "mini-daw")
    target_link_libraries(minidaw PRIVATE minidaw_app)
endif()
