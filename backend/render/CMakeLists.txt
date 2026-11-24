# Rendering: turns a music file (MIDI or MusicXML) into a WAV file with the real engine and no
# audio device. A library; the `piano-lab render` command (backend/app) runs it.

add_library(pianolab_render src/render.cpp)
add_library(pianolab::render ALIAS pianolab_render)
target_include_directories(pianolab_render PUBLIC include)
target_link_libraries(pianolab_render
    PUBLIC pianolab::engine_core
    PRIVATE pianolab::models pianolab::music_io
)
pianolab_set_warnings(pianolab_render)

pianolab_add_tests(pianolab_render_tests
    SOURCES tests/render_tests.cpp
    LINK pianolab::render pianolab::music_io pianolab::juce
)
