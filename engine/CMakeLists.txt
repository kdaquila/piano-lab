# The engine: pure, standard C++ physics and real-time plumbing.
#
# Rule: nothing here may depend on JUCE, networking, JSON, UI code or anything in backend/.
# The check at the bottom enforces it at configure time.

add_subdirectory(engine-core)
add_subdirectory(rt)
add_subdirectory(models)
add_subdirectory(calibration)
add_subdirectory(render-cli)

# --- Dependency rules -------------------------------------------------------------------------

# Each engine target and the only targets it may link to (target=allowed,allowed).
set(engine_allowed_links
    "pianolab_engine_core="
    "pianolab_rt="
    "pianolab_models=pianolab::engine_core"
    "pianolab_calibration=pianolab::engine_core,pianolab::models"
    "pianolab_render_cli=pianolab::engine_core,pianolab::models"
)

foreach(entry IN LISTS engine_allowed_links)
    string(REGEX REPLACE "[=,]" ";" parts "${entry}")
    list(POP_FRONT parts target)
    get_target_property(links ${target} LINK_LIBRARIES)
    if(NOT links)
        set(links "")
    endif()
    foreach(link IN LISTS links)
        if(NOT link IN_LIST parts)
            message(FATAL_ERROR
                "Engine dependency rule broken: ${target} links ${link}. "
                "Engine targets may only link the engine libraries listed in engine/CMakeLists.txt.")
        endif()
    endforeach()
endforeach()
