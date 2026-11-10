# Runs mini-daw with a script on its own config, project and home folder, and
# checks the exit code and the report (P25a, T-RUN-04). Used by ctest:
#   cmake -DMINIDAW=<exe> -DVST3DIR=<folder with the test synth> -DWORK=<folder>
#         -DSCRIPT=<script.txt> -DEXPECT_EXIT=<code> [-DEXPECT_IN_REPORT=<a|b>] -P run_script.cmake
# The script may use @WORK@ (the work folder). mini-daw opens a window: on
# Linux it needs a display (CI runs the tests under xvfb-run).

if(UNIX AND NOT APPLE AND "$ENV{DISPLAY}" STREQUAL "")
    message("SKIPPED: no display (DISPLAY is not set)")
    return()
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/home" "${WORK}/cfg" "${WORK}/takes" "${WORK}/project")
file(WRITE "${WORK}/cfg/config.ini"
"[audio]
input_device = (none)
output_device = (none)
[recording]
folder = \"${WORK}/takes\"
[instruments]
folder1 = \"${VST3DIR}\"
")
# A project with one MIDI track without an instrument.
file(WRITE "${WORK}/project/project.json"
"{\"format\": \"mini-daw project\", \"version\": 1, \"sampleRate\": 48000, \"tracks\": [
  {\"number\": 1, \"name\": \"Keys\", \"kind\": \"midi\", \"midiInput\": \"\", \"midiChannel\": 0,
   \"midiOutput\": \"\", \"armed\": false, \"volumeDb\": 0, \"mute\": false, \"solo\": false,
   \"take\": \"\", \"takeStart\": 0}]}
")
file(READ "${SCRIPT}" text)
string(REPLACE "@WORK@" "${WORK}" text "${text}")
get_filename_component(name "${SCRIPT}" NAME_WE)
file(WRITE "${WORK}/${name}.txt" "${text}")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "HOME=${WORK}/home" "XDG_CONFIG_HOME="
            "${MINIDAW}" --config "${WORK}/cfg/config.ini" --script "${WORK}/${name}.txt"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    TIMEOUT 120)
message("${out}${err}")
set(report "")
if(EXISTS "${WORK}/${name}.report.txt")
    file(READ "${WORK}/${name}.report.txt" report)
    message("--- report ---\n${report}")
endif()
if(NOT "${rc}" STREQUAL "${EXPECT_EXIT}")
    message(FATAL_ERROR "exit code ${rc}, expected ${EXPECT_EXIT}")
endif()
string(REPLACE "|" ";" EXPECT_IN_REPORT "${EXPECT_IN_REPORT}")
foreach(wanted IN LISTS EXPECT_IN_REPORT)
    string(FIND "${report}${err}" "${wanted}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "not in the report: ${wanted}")
    endif()
endforeach()
