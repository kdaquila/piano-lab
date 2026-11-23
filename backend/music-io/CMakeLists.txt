# Music files in and out: Standard MIDI files and MusicXML read into the engine's Performance,
# and WAV files written from rendered samples. JUCE does the low-level parsing.

add_library(pianolab_music_io
    src/files.cpp
    src/midi.cpp
    src/musicxml.cpp
    src/wav.cpp
)
add_library(pianolab::music_io ALIAS pianolab_music_io)
target_include_directories(pianolab_music_io PUBLIC include)
target_link_libraries(pianolab_music_io
    PUBLIC pianolab::engine_core
    PRIVATE pianolab::juce
)
pianolab_set_warnings(pianolab_music_io)

pianolab_add_tests(pianolab_music_io_tests
    SOURCES
        tests/midi_tests.cpp
        tests/musicxml_tests.cpp
        tests/wav_tests.cpp
    LINK pianolab::music_io pianolab::juce
)
