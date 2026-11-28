# Run by the test pianolab_check_mitigations adds (cmake/Hardening.cmake): reads PROGRAM's
# headers with DUMPBIN and fails unless Control Flow Guard and CET compatibility are both on.

execute_process(COMMAND "${DUMPBIN}" /nologo /headers "${PROGRAM}"
                OUTPUT_VARIABLE headers RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "dumpbin couldn't read ${PROGRAM}")
endif()
foreach(expected "Control Flow Guard" "CET compatible")
    string(FIND "${headers}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${PROGRAM} lacks '${expected}'")
    endif()
    message(STATUS "${PROGRAM}: ${expected}")
endforeach()
