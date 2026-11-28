# piano-lab-audio: the audio service's program. Subcommands for its roles: `run` (audio-service/live),
# which the web server starts, and `render` (audio-service/render).

add_executable(pianolab_app src/main.cpp)
set_target_properties(pianolab_app PROPERTIES OUTPUT_NAME piano-lab-audio)
target_link_libraries(pianolab_app PRIVATE
    pianolab::render pianolab::live pianolab::settings pianolab::logging pianolab::music_io
    pianolab::juce)
# The version the audio service reports in its `ready` event.
target_compile_definitions(pianolab_app PRIVATE PIANOLAB_VERSION="${PROJECT_VERSION}")
pianolab_set_warnings(pianolab_app)
# The build checks the compiler's exploit mitigations made it into the program.
pianolab_check_mitigations(pianolab_app)
