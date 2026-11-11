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
    test_auto_record.cpp
    test_timeline.cpp
    test_monitor.cpp
    test_log.cpp
    test_player.cpp
    test_project.cpp
    test_plugins.cpp
    test_presets.cpp
    test_folders.cpp
    test_instance_lock.cpp
    test_terms.cpp
    test_run_loop.cpp
    test_script.cpp
    test_instrument.cpp)
target_link_libraries(minidaw_tests PRIVATE minidaw_core minidaw_warnings Catch2::Catch2WithMain)
# The SDK's own .vstpreset code, for the compatibility test only (crosscheck #16, X12):
# mini-daw reads and writes the format itself.
# (with the SDK's base library it needs, the same files the test synth builds)
set(minidaw_sdk_preset_sources ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstpresetfile.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/baseiids.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fbuffer.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fdebug.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fobject.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/fstring.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/timer.cpp
    ${vst3sdk_SOURCE_DIR}/base/source/updatehandler.cpp
    ${vst3sdk_SOURCE_DIR}/base/thread/source/fcondition.cpp
    ${vst3sdk_SOURCE_DIR}/base/thread/source/flock.cpp)
target_sources(minidaw_tests PRIVATE ${minidaw_sdk_preset_sources})
# The SDK's code, not ours: without our warning flags.
set_source_files_properties(${minidaw_sdk_preset_sources} PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
# The GUI's state cues (PL1, crosscheck #15 W9): a headless Dear ImGui context, no window.
if(TARGET minidaw_imgui)
    target_sources(minidaw_tests PRIVATE test_state_cues.cpp ${PROJECT_SOURCE_DIR}/src/app/state_cues.cpp)
    target_include_directories(minidaw_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
    target_link_libraries(minidaw_tests PRIVATE minidaw_imgui)
endif()

# The test synth (P10c): a VST3 instrument built from the SDK's plugin-side
# sources, as a bundle with the standard layout, for the hosting tests.
set(vst3 ${vst3sdk_SOURCE_DIR})
add_library(minidaw_testsynth MODULE
    vst3_testsynth/testsynth.cpp
    ${vst3}/base/source/baseiids.cpp
    ${vst3}/base/source/fbuffer.cpp
    ${vst3}/base/source/fdebug.cpp
    ${vst3}/base/source/fobject.cpp
    ${vst3}/base/source/fstreamer.cpp
    ${vst3}/base/source/fstring.cpp
    ${vst3}/base/source/timer.cpp
    ${vst3}/base/source/updatehandler.cpp
    ${vst3}/base/thread/source/fcondition.cpp
    ${vst3}/base/thread/source/flock.cpp
    ${vst3}/pluginterfaces/base/conststringtable.cpp
    ${vst3}/pluginterfaces/base/coreiids.cpp
    ${vst3}/pluginterfaces/base/funknown.cpp
    ${vst3}/pluginterfaces/base/ustring.cpp
    ${vst3}/public.sdk/source/common/commoniids.cpp
    ${vst3}/public.sdk/source/common/pluginview.cpp
    ${vst3}/public.sdk/source/main/pluginfactory.cpp
    ${vst3}/public.sdk/source/main/moduleinit.cpp
    ${vst3}/public.sdk/source/vst/vstaudioeffect.cpp
    ${vst3}/public.sdk/source/vst/vstbus.cpp
    ${vst3}/public.sdk/source/vst/vstcomponent.cpp
    ${vst3}/public.sdk/source/vst/vstcomponentbase.cpp
    ${vst3}/public.sdk/source/vst/vsteditcontroller.cpp
    ${vst3}/public.sdk/source/vst/vstinitiids.cpp
    ${vst3}/public.sdk/source/vst/vstparameters.cpp)
if(WIN32)
    target_sources(minidaw_testsynth PRIVATE ${vst3}/public.sdk/source/main/dllmain.cpp)
    set(testsynth_arch x86_64-win)
    set(testsynth_suffix .vst3)
else()
    target_sources(minidaw_testsynth PRIVATE ${vst3}/public.sdk/source/main/linuxmain.cpp)
    set(testsynth_arch x86_64-linux)
    set(testsynth_suffix .so)
endif()
target_include_directories(minidaw_testsynth SYSTEM PRIVATE ${vst3})
target_compile_definitions(minidaw_testsynth PRIVATE $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
if(MSVC)
    target_compile_options(minidaw_testsynth PRIVATE /Zc:__cplusplus)  # see minidaw_vst3host
endif()
set_target_properties(minidaw_testsynth PROPERTIES PREFIX "" OUTPUT_NAME "minidaw-testsynth"
                                                   SUFFIX "${testsynth_suffix}" CXX_VISIBILITY_PRESET hidden)
if(WIN32)
    target_link_libraries(minidaw_testsynth PRIVATE ole32 user32 gdi32)  # gdi32/user32: the editor window
    if(MINGW)
        target_link_options(minidaw_testsynth PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()
if(UNIX AND NOT APPLE)
    # The test synth's X11 editor and the X11 editor test (P24).
    find_package(X11 REQUIRED)
    target_link_libraries(minidaw_testsynth PRIVATE X11::X11)
    target_sources(minidaw_tests PRIVATE test_editor_x11.cpp)
    target_link_libraries(minidaw_tests PRIVATE X11::X11)
endif()
set(testsynth_bundle ${CMAKE_CURRENT_BINARY_DIR}/vst3/minidaw-testsynth.vst3)
add_custom_command(TARGET minidaw_testsynth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${testsynth_bundle}/Contents/${testsynth_arch}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:minidaw_testsynth>
            ${testsynth_bundle}/Contents/${testsynth_arch}/minidaw-testsynth${testsynth_suffix}
    VERBATIM)
add_dependencies(minidaw_tests minidaw_testsynth)
target_compile_definitions(minidaw_tests PRIVATE MINIDAW_TEST_VST3="${testsynth_bundle}")
catch_discover_tests(minidaw_tests DISCOVERY_MODE PRE_TEST)

# mini-daw run by a script with the test synth (P25a, T-RUN-04). It opens a
# window: Linux only for now (CI runs the tests under xvfb-run; without a
# display they are skipped). Each case: script name, exit code, texts the
# report (or the errors) must contain.
if(UNIX AND NOT APPLE AND TARGET minidaw)
    # minidaw_script_test(<name> <exit code> [IN <texts>...] [NOT_IN <texts>...] [HOLD_LOCK])
    function(minidaw_script_test name code)
        cmake_parse_arguments(PARSE_ARGV 2 arg "HOLD_LOCK" "" "IN;NOT_IN")
        set(dir ${PROJECT_SOURCE_DIR}/tests/scripts)
        string(REPLACE ";" "|" wanted "${arg_IN}")  # passed as one value, split again in the script
        string(REPLACE ";" "|" unwanted "${arg_NOT_IN}")
        add_test(NAME "T-RUN-04: script ${name}"
            COMMAND ${CMAKE_COMMAND} -DMINIDAW=$<TARGET_FILE:minidaw> -DVST3DIR=${CMAKE_CURRENT_BINARY_DIR}/vst3
                    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/script-${name} -DSCRIPT=${dir}/${name}.txt
                    -DEXPECT_EXIT=${code} "-DEXPECT_IN_REPORT=${wanted}" "-DEXPECT_NOT_IN_REPORT=${unwanted}"
                    -DHOLD_LOCK=${arg_HOLD_LOCK} -P ${dir}/run_script.cmake)
        set_tests_properties("T-RUN-04: script ${name}" PROPERTIES
            LABELS "script" SKIP_REGULAR_EXPRESSION "SKIPPED: no display" TIMEOUT 150)
    endfunction()
    minidaw_script_test(basic 0 IN "Result: 28 command(s), 0 failed")
    minidaw_script_test(failures 1 IN
        "no instrument \"no such synth\" found" "there is no track 7" "is \"Keys\"" "still false"
        "echo still running" "no track added by the script (last)" "Result: 11 command(s), 5 failed")
    minidaw_script_test(bad 2 IN "line 2: use: open <project folder>" "line 3: unknown command \"plya\""
        "Not run: the script has 2 error(s)")
    minidaw_script_test(stop 1 IN "is \"MIDI 1\"" "Skipped 1 line(s) after the failure"
        "Result: 5 command(s), 1 failed" NOT_IN "echo never")
    minidaw_script_test(quit 0 IN "Result: 2 command(s), 0 failed" NOT_IN "echo never")
    minidaw_script_test(locked 3 IN "the script is not run" NOT_IN "Result:" HOLD_LOCK)
    minidaw_script_test(loglines 0 IN "Track 1 \"MIDI 1\" added" "Track 1 \"MIDI 1\" removed"
        "Track 2 \"MIDI 1 sound\" is now a normal audio track" "Result: 7 command(s), 0 failed"
        NOT_IN "output (no MIDI output)")
endif()

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
