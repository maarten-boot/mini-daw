# Extra check of the script case "exportfail" (crosscheck #22 CC7): the folders
# made for the file are gone again.
if(EXISTS "${WORK}/out/made")
    message(FATAL_ERROR "the failed export left ${WORK}/out/made behind")
endif()
message("exportfail: no folder left behind")
