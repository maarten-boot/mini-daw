# Extra check of the script case "exportaudio" (P29, T-PRJ-12), included by
# run_script.cmake: the FLAC file is there, the zip holds the stem and the
# take's MIDI file, and no work folder is left.
set(flac "${WORK}/out/project song.flac")
if(NOT EXISTS "${flac}")
    message(FATAL_ERROR "${flac} was not written")
endif()
# In hex: as text, CMake's READ of a binary file may return more than LIMIT bytes.
file(READ "${flac}" head LIMIT 4 HEX)
if(NOT head STREQUAL "664c6143")  # "fLaC"
    message(FATAL_ERROR "${flac} is not a FLAC file: ${head}")
endif()
file(SIZE "${flac}" size)
if(size LESS 2000)
    message(FATAL_ERROR "${flac} is too small (${size} bytes): no sound rendered?")
endif()
set(zip "${WORK}/out/Final.zip")
file(REMOVE_RECURSE "${WORK}/unzipped")
file(ARCHIVE_EXTRACT INPUT "${zip}" DESTINATION "${WORK}/unzipped")
foreach(f "project song/project song - 01-Keys.wav" "project song/01-Keys.mid")
    if(NOT EXISTS "${WORK}/unzipped/${f}")
        message(FATAL_ERROR "${zip} does not hold ${f}")
    endif()
endforeach()
file(GLOB left "${WORK}/out/.mini-daw-export-*")
if(left)
    message(FATAL_ERROR "work folder left behind: ${left}")
endif()
message("exportaudio: FLAC and zip as expected")
