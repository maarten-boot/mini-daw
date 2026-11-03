include(Catch)

add_executable(minidaw_tests
    test_version.cpp
    test_devices.cpp
    test_color.cpp
    test_theme.cpp
    test_config.cpp
    test_audio_stream.cpp
    test_wav.cpp
    test_ring_buffer.cpp
    test_recorder.cpp
    test_session.cpp
    test_smf.cpp
    test_track.cpp
    test_monitor.cpp
    test_log.cpp
    test_player.cpp
    test_project.cpp)
target_link_libraries(minidaw_tests PRIVATE minidaw_core minidaw_warnings Catch2::Catch2WithMain)
catch_discover_tests(minidaw_tests DISCOVERY_MODE PRE_TEST)

# Licence checks (T-LIC-02, T-LIC-03). Needs Python 3; skipped if not found.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME licence_check
        COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/check_licenses.py
                --root ${PROJECT_SOURCE_DIR})
    set_tests_properties(licence_check PROPERTIES LABELS "licence")
    # Windows programs: mini-daw.exe without console, mini-daw.com with, both
    # with icon and version information (T-PKG-01).
    if(WIN32 AND TARGET minidaw AND TARGET minidaw_cli)
        add_test(NAME pe_check
            COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/check_pe.py $<TARGET_FILE_DIR:minidaw>)
        set_tests_properties(pe_check PROPERTIES LABELS "package")
    endif()
endif()
