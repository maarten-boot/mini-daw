# T-PRJ-10: XSD — mini-daw's DAWproject export checked against the format's
# schemas (P27). Runs the export test with MINIDAW_DAWPROJECT_OUT=OUT (it then
# writes project.xml and metadata.xml there), then xmllint. Skipped when xmllint is not installed.
#   cmake -DTESTS=<minidaw_tests> -DOUT=<folder> -DXSD=<this folder> -P check_xsd.cmake
cmake_minimum_required(VERSION 3.24)
find_program(XMLLINT xmllint)
if(NOT XMLLINT)
    message("SKIPPED: xmllint is not installed (Debian/Ubuntu: libxml2-utils)")
    return()
endif()
file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND ${CMAKE_COMMAND} -E env "MINIDAW_DAWPROJECT_OUT=${OUT}"
                        "${TESTS}" "T-PRJ-10: a project exported as a .dawproject file"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the export test failed:\n${out}")
endif()
foreach(pair "Project.xsd;project.xml" "MetaData.xsd;metadata.xml")
    list(GET pair 0 xsd)
    list(GET pair 1 xml)
    if(NOT EXISTS "${OUT}/${xml}")
        message(FATAL_ERROR "${OUT}/${xml} was not written")
    endif()
    execute_process(COMMAND "${XMLLINT}" --noout --schema "${XSD}/${xsd}" "${OUT}/${xml}"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
    message("${out}")
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${xml} is not valid against ${xsd}")
    endif()
endforeach()
