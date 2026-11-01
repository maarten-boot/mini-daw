configure_file(core/include/minidaw/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/minidaw/version.hpp @ONLY)

# Core library: everything that is not GUI. Tests link against this.
add_library(minidaw_core STATIC
    core/src/devices.cpp)
target_include_directories(minidaw_core PUBLIC
    core/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(minidaw_core
    PUBLIC  minidaw_rtaudio minidaw_rtmidi
    PRIVATE minidaw_warnings)

if(MINIDAW_BUILD_APP)
    # Console subsystem on Windows for now, so --list-devices output is visible.
    add_executable(minidaw app/main.cpp)
    set_target_properties(minidaw PROPERTIES OUTPUT_NAME "mini-daw")
    target_link_libraries(minidaw PRIVATE minidaw_core minidaw_imgui minidaw_warnings)
endif()
