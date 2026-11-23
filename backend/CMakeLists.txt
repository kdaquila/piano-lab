# The backend: the program that runs the engine. Music files, JUCE audio devices and MIDI, and
# later the web server and control protocol. JUCE is only ever used from here.

add_subdirectory(juce)
add_subdirectory(logging)
add_subdirectory(music-io)
add_subdirectory(server)
add_subdirectory(render)
add_subdirectory(app)
