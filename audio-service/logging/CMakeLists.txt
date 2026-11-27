# Logging: our own small Log interface (log.hpp), implemented with spdlog in one file.

add_library(pianolab_logging src/spdlog_log.cpp)
add_library(pianolab::logging ALIAS pianolab_logging)
target_include_directories(pianolab_logging PUBLIC include)
target_link_libraries(pianolab_logging
    PUBLIC pianolab::engine_core
    PRIVATE spdlog::spdlog
)
pianolab_set_warnings(pianolab_logging)

pianolab_add_tests(pianolab_logging_tests
    SOURCES tests/log_tests.cpp
    LINK pianolab::logging
)
