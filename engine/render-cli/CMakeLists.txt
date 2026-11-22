# render-cli: renders the engine offline to WAV files, with no audio device. Engine-only, no JUCE.

add_executable(pianolab_render_cli src/main.cpp)
set_target_properties(pianolab_render_cli PROPERTIES OUTPUT_NAME piano-lab-render)
target_link_libraries(pianolab_render_cli PRIVATE pianolab::engine_core pianolab::models)
pianolab_set_warnings(pianolab_render_cli)
