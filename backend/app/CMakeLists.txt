# piano-lab: the one program. Subcommands for its roles: `render` (backend/render) and
# `server start` (backend/server).

add_executable(pianolab_app src/main.cpp)
set_target_properties(pianolab_app PROPERTIES OUTPUT_NAME piano-lab)
target_link_libraries(pianolab_app PRIVATE
    pianolab::render pianolab::server pianolab::logging pianolab::music_io pianolab::juce)
pianolab_set_warnings(pianolab_app)
