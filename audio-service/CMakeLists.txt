# The audio service: the program that runs the engine. Music files, JUCE audio devices and
# MIDI, settings and recording. It never touches the network: from step 0.6 it talks only to
# the TypeScript web server, over the service pipe. JUCE is only ever used from here.

add_subdirectory(juce)
add_subdirectory(logging)
add_subdirectory(music-io)
add_subdirectory(settings)
add_subdirectory(pipe)
add_subdirectory(live)
add_subdirectory(render)
add_subdirectory(app)
