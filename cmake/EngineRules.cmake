# Enforces the engine's dependency rule: nothing in engine/ may depend on JUCE, networking, JSON,
# UI code or anything in backend/.
#
# pianolab_engine_rules(DIRECTORY <dir> ALLOW <target>=<allowed>,<allowed>... ...)
#
# Every target defined under <dir> must either be listed in ALLOW with the only targets it may
# link, or be a test executable (name ending in _tests), which may link the listed engine
# libraries and Catch2. Both direct (LINK_LIBRARIES) and usage (INTERFACE_LINK_LIBRARIES) links
# are checked. The check runs at the end of the whole configure, so links added from anywhere
# in the project are caught.

function(pianolab_engine_rules)
    cmake_parse_arguments(ARG "" "DIRECTORY" "ALLOW" ${ARGN})
    set_property(GLOBAL PROPERTY PIANOLAB_ENGINE_RULES_DIR "${ARG_DIRECTORY}")
    set_property(GLOBAL PROPERTY PIANOLAB_ENGINE_RULES_ALLOW "${ARG_ALLOW}")
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _pianolab_check_engine_rules)
endfunction()

# All build targets defined in a directory and its subdirectories.
function(_pianolab_targets_under dir out)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(sub IN LISTS subdirs)
        _pianolab_targets_under("${sub}" subTargets)
        list(APPEND targets ${subTargets})
    endforeach()
    set(${out} ${targets} PARENT_SCOPE)
endfunction()

function(_pianolab_check_engine_rules)
    get_property(dir GLOBAL PROPERTY PIANOLAB_ENGINE_RULES_DIR)
    get_property(rules GLOBAL PROPERTY PIANOLAB_ENGINE_RULES_ALLOW)

    # Parse "target=a,b" entries; engine libraries are the listed targets' pianolab:: aliases.
    set(engineLibraries "")
    foreach(rule IN LISTS rules)
        string(REGEX REPLACE "[=,]" ";" parts "${rule}")
        list(POP_FRONT parts target)
        list(REMOVE_ITEM parts "")
        set(allowed_${target} ${parts})
        set(listed_${target} TRUE)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "LIBRARY$")
            string(REGEX REPLACE "^pianolab_" "pianolab::" alias "${target}")
            list(APPEND engineLibraries "${alias}")
        endif()
    endforeach()

    _pianolab_targets_under("${dir}" targets)
    foreach(target IN LISTS targets)
        if(target MATCHES "_tests$")
            set(allowed ${engineLibraries} Catch2::Catch2WithMain)
        elseif(listed_${target})
            set(allowed ${allowed_${target}})
        else()
            message(FATAL_ERROR
                "Engine dependency rule: ${target} is not listed in engine/CMakeLists.txt. "
                "Add it to pianolab_engine_rules() with the targets it may link.")
        endif()

        foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(links ${target} ${property})
            if(NOT links)
                continue()
            endif()
            foreach(link IN LISTS links)
                # CMake's internal markers for links added from another directory.
                if(link MATCHES "^::@")
                    continue()
                endif()
                # PRIVATE links of static libraries show up as $<LINK_ONLY:target>.
                if(link MATCHES "^\\$<LINK_ONLY:([^$<>]+)>$")
                    set(link "${CMAKE_MATCH_1}")
                endif()
                if(link MATCHES "\\$<")
                    message(FATAL_ERROR
                        "Engine dependency rule: ${target} links '${link}', a generator "
                        "expression the check can't verify. Link engine targets directly.")
                endif()
                if(NOT link IN_LIST allowed)
                    message(FATAL_ERROR
                        "Engine dependency rule broken: ${target} links ${link}. "
                        "Engine targets may only link what engine/CMakeLists.txt allows them.")
                endif()
            endforeach()
        endforeach()
    endforeach()
endfunction()
