# Calibration: fitting model parameters to measurements and listening tests.

add_library(pianolab_calibration src/calibration.cpp)
add_library(pianolab::calibration ALIAS pianolab_calibration)
target_include_directories(pianolab_calibration PUBLIC include)
target_link_libraries(pianolab_calibration PUBLIC pianolab::engine_core pianolab::models)
pianolab_set_warnings(pianolab_calibration)

pianolab_add_tests(pianolab_calibration_tests
    SOURCES tests/calibration_tests.cpp
    LINK pianolab::calibration
)
