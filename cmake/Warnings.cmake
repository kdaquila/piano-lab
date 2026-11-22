# Strict warnings for our own targets. Dependencies never get these: they are added as SYSTEM, so
# their headers are exempt too.

function(pianolab_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4              # high warning level
            /WX              # warnings are errors
            /permissive-     # standards conformance
            /Zc:__cplusplus  # report the real C++ version in __cplusplus
            /Zc:preprocessor # standards-conforming preprocessor
            /utf-8           # source and execution character sets are UTF-8
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        )
    endif()
endfunction()
