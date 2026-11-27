# The service: the program that runs the engine. Music files, JUCE audio devices and MIDI,
# settings and recording. It never touches the network: from step 0.6 it talks only to the
# TypeScript backend, over a pipe (and this folder becomes service/). JUCE is only ever used
# from here.

add_subdirectory(juce)
add_subdirectory(logging)
add_subdirectory(music-io)
add_subdirectory(settings)
add_subdirectory(server)
add_subdirectory(render)
add_subdirectory(app)
