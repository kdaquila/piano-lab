# AddressSanitizer for the whole build, dependencies included.
#
# It has to be global: MSVC refuses to link code built with and without ASan when both use
# standard containers (the "annotate_vector" mismatch), so every target gets the same flag.
# Included before any dependency is added, so the directory-wide options reach them too.

if(NOT PIANOLAB_ASAN)
    return()
endif()

if(MSVC)
    add_compile_options(/fsanitize=address /Zi)
    # Edit-and-continue and incremental linking are incompatible with ASan.
    add_link_options(/INCREMENTAL:NO /DEBUG)
else()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
