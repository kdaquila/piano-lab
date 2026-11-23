# The piano server: runs the engine live against audio and MIDI devices.
#
# The audio core (live MIDI, the per-block audio work, the recorder and the monitor) is free of
# JUCE, so tests drive it block by block like a sound card would. server.cpp is the thin JUCE
# layer: device callbacks and the main loop.

add_library(pianolab_server
    src/audio_core.cpp
    src/live_midi.cpp
    src/monitor.cpp
    src/recorder.cpp
    src/server.cpp
)
add_library(pianolab::server ALIAS pianolab_server)
target_include_directories(pianolab_server PUBLIC include)
target_link_libraries(pianolab_server
    PUBLIC pianolab::engine_core pianolab::models pianolab::rt pianolab::logging pianolab::music_io
    PRIVATE pianolab::juce
)
pianolab_set_warnings(pianolab_server)

pianolab_add_tests(pianolab_server_tests
    SOURCES
        tests/live_midi_tests.cpp
        tests/audio_core_tests.cpp
    LINK pianolab::server pianolab::music_io pianolab::juce
)

# The audio core must never allocate on the audio thread. Like the engine's check, this replaces
# the global allocator, so it's its own executable.
pianolab_add_tests(pianolab_server_no_alloc_tests
    SOURCES tests/no_alloc_tests.cpp
    LINK pianolab::server
)
pianolab_count_allocations(pianolab_server_no_alloc_tests)
