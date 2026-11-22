# Physical models of piano keys: strings, hammers, bridge, soundboard.

add_library(pianolab_models src/models.cpp)
add_library(pianolab::models ALIAS pianolab_models)
target_include_directories(pianolab_models PUBLIC include)
target_link_libraries(pianolab_models PUBLIC pianolab::engine_core)
pianolab_set_warnings(pianolab_models)

pianolab_add_tests(pianolab_models_tests
    SOURCES tests/models_tests.cpp
    LINK pianolab::models
)
