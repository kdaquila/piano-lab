cmake_minimum_required(VERSION 3.28)

project(piano-lab
    VERSION 0.1.0
    DESCRIPTION "A physically modelled piano"
    LANGUAGES C CXX)

# The audio service pulls in JUCE, which is large. Turning it off builds and tests the pure sound
# engine alone, which also proves the sound engine never needs JUCE.
option(PIANOLAB_BUILD_AUDIO_SERVICE "Build the JUCE audio service (devices, MIDI, settings)" ON)
option(PIANOLAB_BUILD_TESTS "Build the test suites" ON)
option(PIANOLAB_ASAN "Build everything with AddressSanitizer" OFF)
# ASIO (Windows): selectable, never the default device. JUCE bundles Steinberg's ASIO SDK headers,
# licensed under the Steinberg ASIO licence or GPLv3; a distributed binary with ASIO must follow
# one of them (checked before release; see docs/vision.md#openness). Turn off for an ASIO-free build.
if(WIN32)
    option(PIANOLAB_ASIO "Support ASIO audio devices (Windows)" ON)
else()
    set(PIANOLAB_ASIO OFF)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# We don't use C++20 modules, so skip the per-file module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Sanitizers)
include(Hardening)
include(Warnings)
include(Dependencies)
include(Testing)
include(EngineRules)

if(PIANOLAB_BUILD_TESTS)
    enable_testing()
    include(Catch)
endif()

add_subdirectory(sound-engine)

if(PIANOLAB_BUILD_AUDIO_SERVICE)
    add_subdirectory(audio-service)
endif()
