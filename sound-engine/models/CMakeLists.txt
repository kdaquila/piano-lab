# Physical models of piano keys: strings, hammers, bridge, soundboard. Today: the sine key, and
# PianoKey, the variant of every model the keyboard can hold.

add_library(pianolab_models
    src/piano_key.cpp
    src/sine_key.cpp
)
add_library(pianolab::models ALIAS pianolab_models)
target_include_directories(pianolab_models PUBLIC include)
target_link_libraries(pianolab_models PUBLIC pianolab::engine_core)
pianolab_set_warnings(pianolab_models)

pianolab_add_tests(pianolab_models_tests
    SOURCES tests/sine_tests.cpp
    LINK pianolab::models
)

# Its own executable: it replaces the global allocator for the whole program.
pianolab_add_tests(pianolab_models_no_alloc_tests
    SOURCES tests/no_alloc_tests.cpp
    LINK pianolab::models
)
pianolab_count_allocations(pianolab_models_no_alloc_tests)
