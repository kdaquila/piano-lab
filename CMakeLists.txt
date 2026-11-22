cmake_minimum_required(VERSION 3.28)

project(piano-lab
    VERSION 0.1.0
    DESCRIPTION "A physically modelled piano"
    LANGUAGES C CXX)

# The backend pulls in JUCE, which is large. Turning it off builds and tests the pure engine alone,
# which also proves the engine never needs JUCE.
option(PIANOLAB_BUILD_BACKEND "Build the JUCE backend (audio devices, MIDI, server)" ON)
option(PIANOLAB_BUILD_TESTS "Build the test suites" ON)
option(PIANOLAB_ASAN "Build everything with AddressSanitizer" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# We don't use C++20 modules, so skip the per-file module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Sanitizers)
include(Warnings)
include(Dependencies)
include(Testing)
include(EngineRules)

if(PIANOLAB_BUILD_TESTS)
    enable_testing()
    include(Catch)
endif()

add_subdirectory(engine)

if(PIANOLAB_BUILD_BACKEND)
    add_subdirectory(backend)
endif()
