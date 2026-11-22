# pianolab::juce: the JUCE modules we use, compiled once into a static library.
#
# JUCE modules are source code that gets compiled into whatever links them. Compiling them here,
# in their own target, keeps our strict warnings (/W4 /WX) off JUCE's code, keeps JUCE's
# configuration (#defines) in this one place, and builds JUCE once for all backend targets.
# Its headers reach our code as SYSTEM includes, so they are exempt from our warnings too.

add_library(pianolab_juce STATIC)
add_library(pianolab::juce ALIAS pianolab_juce)

target_link_libraries(pianolab_juce
    PRIVATE
        juce::juce_audio_devices # brings juce_core, juce_events and juce_audio_basics with it
    PUBLIC
        juce::juce_recommended_config_flags
)

target_compile_definitions(pianolab_juce PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 # configuration comes from here, not an AppConfig.h
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_CURL=0                        # no networking through JUCE
    JUCE_WEB_BROWSER=0
    JUCE_ASIO=0                            # ASIO is decided in step 0.3
)

# Pass the modules' include paths and definitions on to whatever links this library, marked SYSTEM.
target_include_directories(pianolab_juce SYSTEM INTERFACE
    $<TARGET_PROPERTY:pianolab_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(pianolab_juce INTERFACE
    $<TARGET_PROPERTY:pianolab_juce,COMPILE_DEFINITIONS>)
