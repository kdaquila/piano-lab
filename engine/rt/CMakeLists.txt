# Real-time plumbing shared by the engine and the backend, such as lock-free queues. Standard C++ only.

add_library(pianolab_rt src/rt.cpp)
add_library(pianolab::rt ALIAS pianolab_rt)
target_include_directories(pianolab_rt PUBLIC include)
pianolab_set_warnings(pianolab_rt)

pianolab_add_tests(pianolab_rt_tests
    SOURCES tests/rt_tests.cpp
    LINK pianolab::rt
)
