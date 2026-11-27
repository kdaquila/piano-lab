# Real-time plumbing shared by the engine and the audio service: lock-free queues between threads.
# Standard C++ only. Header-only: the queue is a template.

add_library(pianolab_rt INTERFACE)
add_library(pianolab::rt ALIAS pianolab_rt)
target_include_directories(pianolab_rt INTERFACE include)
# rigtorp/SPSCQueue: a single standard-C++ header, so it's used directly rather than linked.
target_include_directories(pianolab_rt SYSTEM INTERFACE "${SPSCQueue_SOURCE_DIR}/include")

pianolab_add_tests(pianolab_rt_tests
    SOURCES tests/spsc_queue_tests.cpp
    LINK pianolab::rt
)
