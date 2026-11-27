# piano-lab: the one program. Subcommands for its roles: `render` (audio-service/render) and
# `server start` (audio-service/live).

add_executable(pianolab_app src/main.cpp)
set_target_properties(pianolab_app PROPERTIES OUTPUT_NAME piano-lab)
target_link_libraries(pianolab_app PRIVATE
    pianolab::render pianolab::live pianolab::settings pianolab::logging pianolab::music_io
    pianolab::juce)
# The version the audio service reports in its `ready` event.
target_compile_definitions(pianolab_app PRIVATE PIANOLAB_VERSION="${PROJECT_VERSION}")
pianolab_set_warnings(pianolab_app)
