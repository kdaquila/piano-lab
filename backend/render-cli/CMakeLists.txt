# render-cli (piano-lab-render): renders a music file (MIDI or MusicXML) to a WAV file with the
# real engine and no audio device. The pipeline is a small library, so tests can drive it.

add_library(pianolab_render src/render.cpp)
add_library(pianolab::render ALIAS pianolab_render)
target_include_directories(pianolab_render PUBLIC include)
target_link_libraries(pianolab_render
    PUBLIC pianolab::engine_core
    PRIVATE pianolab::models pianolab::music_io
)
pianolab_set_warnings(pianolab_render)

add_executable(pianolab_render_cli src/main.cpp)
set_target_properties(pianolab_render_cli PROPERTIES OUTPUT_NAME piano-lab-render)
target_link_libraries(pianolab_render_cli PRIVATE pianolab::render pianolab::music_io)
pianolab_set_warnings(pianolab_render_cli)

pianolab_add_tests(pianolab_render_tests
    SOURCES tests/render_tests.cpp
    LINK pianolab::render pianolab::music_io pianolab::juce
)
