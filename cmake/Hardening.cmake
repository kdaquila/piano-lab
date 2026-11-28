# The compiler's exploit mitigations, on everything we build with MSVC (our code and the
# dependencies compiled with it), so a memory bug is harder to turn into running an attacker's
# code:
#   /guard:cf     Control Flow Guard: indirect calls may only reach real function starts
#   /CETCOMPAT    marks programs compatible with CET shadow stacks (hardware-checked returns)
# Not with AddressSanitizer, which doesn't support Control Flow Guard.
#
# pianolab_check_mitigations(<executable target>) adds a test that reads the built program's
# headers with dumpbin and fails unless both are there.

if(MSVC AND NOT PIANOLAB_ASAN)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf /CETCOMPAT)
endif()

function(pianolab_check_mitigations target)
    if(NOT MSVC OR PIANOLAB_ASAN OR NOT PIANOLAB_BUILD_TESTS)
        return()
    endif()
    find_program(PIANOLAB_DUMPBIN dumpbin REQUIRED)
    add_test(NAME "${target}: has the exploit mitigations"
             COMMAND ${CMAKE_COMMAND} -DDUMPBIN=${PIANOLAB_DUMPBIN}
                     -DPROGRAM=$<TARGET_FILE:${target}>
                     -P "${PROJECT_SOURCE_DIR}/cmake/CheckMitigations.cmake")
endfunction()
