# T-PRJ-10: XSD — mini-daw's DAWproject export checked against the format's
# schemas (P27, crosscheck #22 CC8). Runs the export tests with
# MINIDAW_DAWPROJECT_OUT=OUT (they then copy their .dawproject files there),
# unpacks each with CMake's own ZIP reader (libarchive, not the miniz that wrote
# them), checks that every file the project names (File / State path) is in it,
# and checks project.xml and metadata.xml with xmllint. Skipped when xmllint is
# not installed.
#   cmake -DTESTS=<minidaw_tests> -DOUT=<folder> -DXSD=<this folder> -P check_xsd.cmake
cmake_minimum_required(VERSION 3.24)
find_program(XMLLINT xmllint)
if(NOT XMLLINT)
    message("SKIPPED: xmllint is not installed (Debian/Ubuntu: libxml2-utils)")
    return()
endif()
file(REMOVE_RECURSE "${OUT}")
execute_process(COMMAND ${CMAKE_COMMAND} -E env "MINIDAW_DAWPROJECT_OUT=${OUT}" "${TESTS}" "[dawproject]"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the export tests failed:\n${out}")
endif()
file(GLOB exports "${OUT}/*.dawproject")
list(LENGTH exports count)
if(count LESS 6)
    message(FATAL_ERROR "only ${count} export(s) in ${OUT}, expected 6")
endif()
foreach(export IN LISTS exports)
    get_filename_component(stem "${export}" NAME_WE)
    set(dir "${OUT}/${stem}")
    file(ARCHIVE_EXTRACT INPUT "${export}" DESTINATION "${dir}")
    foreach(pair "Project.xsd;project.xml" "MetaData.xsd;metadata.xml")
        list(GET pair 0 xsd)
        list(GET pair 1 xml)
        if(NOT EXISTS "${dir}/${xml}")
            message(FATAL_ERROR "${stem}: ${xml} is not in the file")
        endif()
        execute_process(COMMAND "${XMLLINT}" --noout --schema "${XSD}/${xsd}" "${dir}/${xml}"
                        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
        message("${stem}: ${out}")
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "${stem}: ${xml} is not valid against ${xsd}")
        endif()
    endforeach()
    # Every file the project refers to is in the ZIP (the test names have no XML escapes).
    file(READ "${dir}/project.xml" xml)
    string(REGEX MATCHALL "<(File|State) path=\"[^\"]*\"" refs "${xml}")
    foreach(ref IN LISTS refs)
        string(REGEX REPLACE "^<(File|State) path=\"([^\"]*)\"$" "\\2" path "${ref}")
        if(NOT EXISTS "${dir}/${path}")
            message(FATAL_ERROR "${stem}: ${path} is named in project.xml but not in the file")
        endif()
        file(SIZE "${dir}/${path}" size)
        if(size EQUAL 0)
            message(FATAL_ERROR "${stem}: ${path} is empty")
        endif()
    endforeach()
endforeach()
message("${count} export(s) checked")
