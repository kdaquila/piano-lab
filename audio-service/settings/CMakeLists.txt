# Settings: the user's settings.toml, read and saved through the field tables, and changed by
# name. toml++ (header-only) is used only by src/toml_reading.cpp.

add_library(pianolab_settings
    src/editing.cpp
    src/settings.cpp
    src/toml_reading.cpp
)
add_library(pianolab::settings ALIAS pianolab_settings)
target_include_directories(pianolab_settings PUBLIC include)
target_include_directories(pianolab_settings SYSTEM PRIVATE "${tomlplusplus_SOURCE_DIR}/include")
# toml++'s configuration, in one place: no exceptions (parsing returns a result instead), and no
# formatters (we write the starter file ourselves).
target_compile_definitions(pianolab_settings PRIVATE TOML_EXCEPTIONS=0 TOML_ENABLE_FORMATTERS=0)
target_link_libraries(pianolab_settings
    PUBLIC pianolab::engine_core pianolab::models
    PRIVATE pianolab::music_io
)
pianolab_set_warnings(pianolab_settings)

pianolab_add_tests(pianolab_settings_tests
    SOURCES tests/settings_tests.cpp
    LINK pianolab::settings pianolab::music_io
)
