# The piano-lab program: runs the engine against real audio and MIDI devices (from step 0.4).

add_executable(pianolab_app src/main.cpp)
set_target_properties(pianolab_app PROPERTIES OUTPUT_NAME piano-lab)
target_link_libraries(pianolab_app PRIVATE pianolab::engine_core pianolab::juce)
pianolab_set_warnings(pianolab_app)
