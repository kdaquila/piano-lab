# Every third-party dependency, pinned to an exact release and checked by SHA-256.
# Upgrades are deliberate: change the version and the hash together.
#
# Downloads are shared between checkouts through CPM's source cache. Set the CPM_SOURCE_CACHE
# environment variable to choose where; otherwise it goes in the user's local cache folder.

if(NOT DEFINED ENV{CPM_SOURCE_CACHE} AND NOT DEFINED CPM_SOURCE_CACHE)
    if(WIN32 AND DEFINED ENV{LOCALAPPDATA} AND NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
        set(cpm_cache_default "$ENV{LOCALAPPDATA}/CPM")
    elseif(NOT WIN32 AND DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        set(cpm_cache_default "$ENV{HOME}/.cache/CPM")
    else()
        # No user folder in this environment (e.g. a service account): keep it in the build tree.
        set(cpm_cache_default "${CMAKE_BINARY_DIR}/_cpm-cache")
    endif()
    set(CPM_SOURCE_CACHE "${cpm_cache_default}" CACHE PATH "CPM download cache")
endif()

include(CPM) # cmake/CPM.cmake, v0.43.2, vendored

# The lock-free queue behind rt::SpscQueue (MIT). A single header with only standard includes, so
# the engine's rt library uses it directly (see docs/vision.md#dependency-rules).
CPMAddPackage(
    NAME SPSCQueue
    VERSION 2.0.1
    URL https://github.com/rigtorp/SPSCQueue/archive/refs/tags/v2.0.1.tar.gz
    URL_HASH SHA256=b2aa6bb53388f55778c704f80cb941e2208f447ad026601d189a8cb31acf5f45
    DOWNLOAD_ONLY YES
)

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

    # Logging (MIT), used only behind our own Log interface (backend/logging). Formats with the
    # standard library's std::format instead of bundling the fmt library. On Windows, file names
    # are UTF-16, so log paths with any characters work.
    if(WIN32)
        set(pianolab_spdlog_wchar ON)
    else()
        set(pianolab_spdlog_wchar OFF)
    endif()
    CPMAddPackage(
        NAME spdlog
        VERSION 1.17.0
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
        SYSTEM YES
        EXCLUDE_FROM_ALL YES
        OPTIONS
            "SPDLOG_USE_STD_FORMAT ON"
            "SPDLOG_WCHAR_FILENAMES ${pianolab_spdlog_wchar}"
            "SPDLOG_BUILD_EXAMPLE OFF"
            "SPDLOG_BUILD_TESTS OFF"
            "SPDLOG_BUILD_BENCH OFF"
            "SPDLOG_INSTALL OFF"
    )

    # Settings files (MIT), read only behind our own settings library (backend/settings), from one
    # .cpp file. Header-only. GitHub's "legacy" tarball of the tag (its folder is named after the
    # commit, marzer-tomlplusplus-efd80e0); DOWNLOAD_NAME gives it an extension CMake recognises.
    CPMAddPackage(
        NAME tomlplusplus
        VERSION 3.5.1
        URL https://codeload.github.com/marzer/tomlplusplus/legacy.tar.gz/refs/tags/v3.5.1
        URL_HASH SHA256=1792eb7a843366f54b777d170dd70829ae3548d0440f71039a2f10c2f1fd4107
        DOWNLOAD_NAME tomlplusplus-3.5.1.tar.gz
        DOWNLOAD_ONLY YES
    )
endif()
