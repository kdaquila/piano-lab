# Core engine types: the key-model concept, the 88-key keyboard, note events, diagnostics,
# offline rendering and tuning. Depends on nothing.

add_library(pianolab_engine_core
    src/diagnostics.cpp
    src/keyboard.cpp
    src/performance.cpp
    src/render_config.cpp
    src/tuning.cpp
)
add_library(pianolab::engine_core ALIAS pianolab_engine_core)
target_include_directories(pianolab_engine_core PUBLIC include)
pianolab_set_warnings(pianolab_engine_core)

pianolab_add_tests(pianolab_engine_core_tests
    SOURCES
        tests/config_tests.cpp
        tests/diagnostics_tests.cpp
        tests/keyboard_tests.cpp
        tests/performance_tests.cpp
        tests/render_offline_tests.cpp
        tests/tuning_tests.cpp
    LINK pianolab::engine_core
)
