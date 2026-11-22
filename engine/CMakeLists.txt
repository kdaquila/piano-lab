# The engine: pure, standard C++ physics and real-time plumbing.
#
# Rule: nothing here may depend on JUCE, networking, JSON, UI code or anything in backend/.
# pianolab_engine_rules() below enforces it when the project is configured (cmake/EngineRules.cmake).

add_subdirectory(engine-core)
add_subdirectory(rt)
add_subdirectory(models)
add_subdirectory(calibration)
add_subdirectory(render-cli)

# Each engine target and the only targets it may link (target=allowed,allowed). Test executables
# (*_tests) may link any engine library plus Catch2. A new engine target must be added here.
pianolab_engine_rules(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    ALLOW
        "pianolab_engine_core="
        "pianolab_rt="
        "pianolab_models=pianolab::engine_core"
        "pianolab_calibration=pianolab::engine_core,pianolab::models"
        "pianolab_render_cli=pianolab::engine_core,pianolab::models"
)
