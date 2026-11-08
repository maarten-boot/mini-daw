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
    test_instrument.cpp)
target_link_libraries(minidaw_tests PRIVATE minidaw_core minidaw_warnings Catch2::Catch2WithMain)
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
set(testsynth_bundle ${CMAKE_CURRENT_BINARY_DIR}/vst3/minidaw-testsynth.vst3)
add_custom_command(TARGET minidaw_testsynth POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${testsynth_bundle}/Contents/${testsynth_arch}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:minidaw_testsynth>
            ${testsynth_bundle}/Contents/${testsynth_arch}/minidaw-testsynth${testsynth_suffix}
    VERBATIM)
add_dependencies(minidaw_tests minidaw_testsynth)
target_compile_definitions(minidaw_tests PRIVATE MINIDAW_TEST_VST3="${testsynth_bundle}")
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
