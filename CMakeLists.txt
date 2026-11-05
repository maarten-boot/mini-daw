cmake_minimum_required(VERSION 3.24)

project(minidaw
    VERSION 0.1.0
    DESCRIPTION "Small MIDI/audio recorder with VST3 instrument hosting"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINIDAW_BUILD_TESTS "Build the unit tests" ON)
option(MINIDAW_BUILD_APP   "Build the GUI application" ON)

# --- ASIO (SPEC-LIC-03) -----------------------------------------------------
# ASIO is optional and OFF by default. The ASIO SDK is never part of this
# repository; you download it yourself from Steinberg. Steinberg offers it
# under GPLv3 or a proprietary licence. A binary built with ASIO under GPLv3
# is itself GPLv3. See README.md.
option(MINIDAW_WITH_ASIO "Enable the ASIO audio backend (Windows only, see README)" OFF)
set(ASIO_SDK_DIR "" CACHE PATH "Path to the unpacked Steinberg ASIO SDK")

if(MINIDAW_WITH_ASIO)
    if(NOT WIN32)
        message(FATAL_ERROR "MINIDAW_WITH_ASIO is only supported on Windows.")
    endif()
    if(ASIO_SDK_DIR STREQUAL "" OR NOT EXISTS "${ASIO_SDK_DIR}/common/asio.h")
        message(FATAL_ERROR
            "MINIDAW_WITH_ASIO=ON needs the Steinberg ASIO SDK.\n"
            "  1. Download it from https://www.steinberg.net/developers/asiosdk-open/\n"
            "  2. Unpack it and pass -DASIO_SDK_DIR=<path> (the folder that contains common/asio.h).\n"
            "Licence: the ASIO SDK is GPLv3 or Steinberg-proprietary. A binary built with ASIO\n"
            "under GPLv3 must be distributed under GPLv3. The mini-daw source code itself stays MIT.")
    endif()
    message(FATAL_ERROR "ASIO support is planned for step P14 and is not implemented yet.")
endif()

# Make warnings visible for our own code only.
add_library(minidaw_warnings INTERFACE)
if(MSVC)
    target_compile_options(minidaw_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(minidaw_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(MINIDAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
