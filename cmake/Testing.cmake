# pianolab_add_tests(<name> SOURCES <files>... LINK <targets>...)
#
# One Catch2 test executable, registered with CTest. Tests are discovered when CTest runs (not at
# build time), so building never has to launch the test binary.

function(pianolab_add_tests name)
    if(NOT PIANOLAB_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(ARG "" "" "SOURCES;LINK" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LINK} Catch2::Catch2WithMain)
    pianolab_set_warnings(${name})
    # CTest names must be unique across all executables, so each test is prefixed with its
    # executable's name: two libraries may both have a test called "is deterministic".
    catch_discover_tests(${name} DISCOVERY_MODE PRE_TEST TEST_PREFIX "${name}: ")
endfunction()
