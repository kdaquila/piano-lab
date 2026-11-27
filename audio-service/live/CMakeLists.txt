# The live piano: runs the engine live against audio and MIDI devices, driven over the service
# pipe.
#
# The audio core (live MIDI, the per-block audio work, the recorder and the monitor), the rules
# for choosing devices and the service's command handling are free of JUCE, so tests drive them
# without devices: the audio core block by block like a sound card would. server.cpp is the thin
# JUCE layer: device callbacks, the devices behind the service's hooks, and the main loop.

add_library(pianolab_live
    src/audio_core.cpp
    src/devices.cpp
    src/live_midi.cpp
    src/monitor.cpp
    src/pipe_io.cpp
    src/recorder.cpp
    src/server.cpp
    src/service.cpp
)
add_library(pianolab::live ALIAS pianolab_live)
target_include_directories(pianolab_live PUBLIC include)
target_link_libraries(pianolab_live
    PUBLIC pianolab::engine_core pianolab::models pianolab::rt pianolab::logging pianolab::music_io
        pianolab::settings pianolab::pipe
    PRIVATE pianolab::juce
)
pianolab_set_warnings(pianolab_live)

pianolab_add_tests(pianolab_live_tests
    SOURCES
        tests/live_midi_tests.cpp
        tests/audio_core_tests.cpp
        tests/devices_tests.cpp
        tests/service_tests.cpp
    LINK pianolab::live pianolab::music_io pianolab::juce
)

# The audio core must never allocate on the audio thread. Like the engine's check, this replaces
# the global allocator, so it's its own executable.
pianolab_add_tests(pianolab_live_no_alloc_tests
    SOURCES tests/no_alloc_tests.cpp
    LINK pianolab::live
)
pianolab_count_allocations(pianolab_live_no_alloc_tests)
