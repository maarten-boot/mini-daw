include(Catch)

add_executable(minidaw_tests
    test_version.cpp
    test_devices.cpp
    test_color.cpp
    test_theme.cpp
    test_config.cpp
    test_audio_stream.cpp)
target_link_libraries(minidaw_tests PRIVATE minidaw_core minidaw_warnings Catch2::Catch2WithMain)
catch_discover_tests(minidaw_tests DISCOVERY_MODE PRE_TEST)

# Licence checks (T-LIC-02, T-LIC-03). Needs Python 3; skipped if not found.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME licence_check
        COMMAND Python3::Interpreter -I ${PROJECT_SOURCE_DIR}/tools/check_licenses.py
                --root ${PROJECT_SOURCE_DIR})
    set_tests_properties(licence_check PROPERTIES LABELS "licence")
endif()
