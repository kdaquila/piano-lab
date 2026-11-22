# Core engine types: key models, the 88-key keyboard, note events, offline rendering, tuning. Depends on nothing.

add_library(pianolab_engine_core src/engine_core.cpp)
add_library(pianolab::engine_core ALIAS pianolab_engine_core)
target_include_directories(pianolab_engine_core PUBLIC include)
pianolab_set_warnings(pianolab_engine_core)

pianolab_add_tests(pianolab_engine_core_tests
    SOURCES tests/engine_core_tests.cpp
    LINK pianolab::engine_core
)
