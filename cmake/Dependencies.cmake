# Every third-party dependency, pinned to an exact release and checked by SHA-256.
# Upgrades are deliberate: change the version and the hash together.
#
# Downloads are shared between checkouts through CPM's source cache. Set the CPM_SOURCE_CACHE
# environment variable to choose where; otherwise it goes in the user's local cache folder.

if(NOT DEFINED ENV{CPM_SOURCE_CACHE} AND NOT DEFINED CPM_SOURCE_CACHE)
    if(WIN32)
        set(CPM_SOURCE_CACHE "$ENV{LOCALAPPDATA}/CPM" CACHE PATH "CPM download cache")
    else()
        set(CPM_SOURCE_CACHE "$ENV{HOME}/.cache/CPM" CACHE PATH "CPM download cache")
    endif()
endif()

include(CPM) # cmake/CPM.cmake, v0.43.2, vendored

if(PIANOLAB_BUILD_TESTS)
    CPMAddPackage(
        NAME Catch2
        VERSION 3.16.1
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
        URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
        SYSTEM YES
        EXCLUDE_FROM_ALL YES
    )
    list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
endif()

if(PIANOLAB_BUILD_BACKEND)
    # JUCE is dual-licensed (AGPLv3 or the JUCE licence); see docs/vision.md#openness.
    CPMAddPackage(
        NAME JUCE
        VERSION 9.0.6
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.6.tar.gz
        URL_HASH SHA256=103e6c9d5f17f19bc7b5485a890573609d326513aad4db6b045181f4936ef424
        SYSTEM YES
        EXCLUDE_FROM_ALL YES
    )
endif()
