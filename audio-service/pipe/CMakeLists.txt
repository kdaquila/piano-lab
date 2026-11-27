# The service pipe's messages: JSON-RPC 2.0, one per line, between the web server and the audio
# service. Free of JUCE and devices. Glaze (header-only) is used only by src/messages.cpp.

add_library(pianolab_pipe
    src/messages.cpp
)
add_library(pianolab::pipe ALIAS pianolab_pipe)
target_include_directories(pianolab_pipe PUBLIC include)
target_include_directories(pianolab_pipe SYSTEM PRIVATE "${glaze_SOURCE_DIR}/include")
pianolab_set_warnings(pianolab_pipe)

pianolab_add_tests(pianolab_pipe_tests
    SOURCES tests/messages_tests.cpp
    LINK pianolab::pipe
)
