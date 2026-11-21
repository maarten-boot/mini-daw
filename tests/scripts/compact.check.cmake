# Extra check of the script case "compact" (P31, T-REC-36), included by
# run_script.cmake: the WAV became FLAC, the project names the FLAC, and the
# song exported from it is as long as the take.
set(take "${WORK}/project/takes/take-20261121-130000")
if(EXISTS "${take}/01-Vocal.wav")
    message(FATAL_ERROR "the WAV is still there")
endif()
file(READ "${take}/01-Vocal.flac" head LIMIT 4 HEX)
if(NOT head STREQUAL "664c6143")  # "fLaC"
    message(FATAL_ERROR "01-Vocal.flac is not a FLAC file: ${head}")
endif()
file(READ "${WORK}/project/project.json" json)
string(FIND "${json}" "01-Vocal.flac" at)
if(at EQUAL -1)
    message(FATAL_ERROR "project.json does not name the FLAC:\n${json}")
endif()
file(SIZE "${WORK}/out/project song.wav" size)
if(NOT size EQUAL 28844)  # 44-byte header + 4800 frames x 2 channels x 3 bytes
    message(FATAL_ERROR "the exported song has ${size} bytes, expected 28844")
endif()
message("compact: FLAC in place, project updated, the song exported from it")
