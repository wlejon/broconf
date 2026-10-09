cmake_minimum_required(VERSION 3.24)

project(broconf VERSION 0.1.0 LANGUAGES CXX)

# Dependencies (cmake/bro_deps.cmake): an outer project's target wins, else the
# ../<name> working tree beside the top-level project, else the pinned commit,
# fetched at configure.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)

# Targets
#   broconf::broconf   typed, schema'd, layered desktop settings store
#
# Sources:
#   src/common   portable (values, schemas, keyfile, layered storage, Store)
#   src/linux    change notification: D-Bus signal (sd-bus) + inotify
#   src/win      change notification: directory watch
#   src/mac      change notification: kqueue directory watch

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_broconf_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_broconf_top_level ON)
endif()

option(BROCONF_BUILD_TESTS "Build the broconf tests" ${_broconf_top_level})

# Coverage. Scoped to a top-level build: a consumer that add_subdirectory()s
# broconf has no business having its own build instrumented from in here.
option(BROCONF_COVERAGE "Instrument the build for gcov coverage (GCC/Clang)" OFF)
if(BROCONF_COVERAGE AND _broconf_top_level)
    if(MSVC)
        message(FATAL_ERROR "BROCONF_COVERAGE requires GCC or Clang (gcov instrumentation)")
    endif()
    # Atomic counters: the notifier runs its own thread, and racing plain
    # counters leave negative branch counts gcov cannot read.
    add_compile_options(--coverage -fprofile-update=atomic -O0 -g)
    add_link_options(--coverage)
    message(STATUS "broconf: coverage instrumentation ON (-O0)")
endif()

# The DLL CRT (/MD, /MDd), as bro uses. broconf_api's tests link
# bronze_runtime_shared, a DLL whose embed boundary passes C++ objects
# (std::string, ...) both ways: with the static CRT the DLL and the test
# executable each get their own heap, and a Debug build asserts
# (__acrt_first_block == header) the first time one frees the other's memory.
if(MSVC AND _broconf_top_level)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

function(broconf_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

file(GLOB BROCONF_SOURCES CONFIGURE_DEPENDS src/common/*.cpp)

if(WIN32)
    file(GLOB _win_sources CONFIGURE_DEPENDS src/win/*.cpp)
    list(APPEND BROCONF_SOURCES ${_win_sources})
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    pkg_check_modules(BROCONF_SYSTEMD REQUIRED IMPORTED_TARGET libsystemd>=246)
    file(GLOB _linux_sources CONFIGURE_DEPENDS src/linux/*.cpp)
    list(APPEND BROCONF_SOURCES ${_linux_sources})
elseif(APPLE)
    find_package(Threads REQUIRED)
    file(GLOB _mac_sources CONFIGURE_DEPENDS src/mac/*.cpp)
    list(APPEND BROCONF_SOURCES ${_mac_sources})
else()
    message(FATAL_ERROR "broconf implements Windows, Linux and macOS backends only")
endif()

add_library(broconf STATIC ${BROCONF_SOURCES})
add_library(broconf::broconf ALIAS broconf)

target_include_directories(broconf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

broconf_warnings(broconf)

if(WIN32)
    target_compile_definitions(broconf PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(broconf
        PUBLIC
            Threads::Threads
        PRIVATE
            PkgConfig::BROCONF_SYSTEMD)
elseif(APPLE)
    target_link_libraries(broconf PUBLIC Threads::Threads)
endif()

if(BROCONF_BUILD_TESTS)
    enable_testing()
endif()

# --- Standalone Bronze JavaScript API ---
# On by default only when broconf is the top-level project. A library that
# consumes broconf then never needs bronze for it; bro forces
# BROCONF_ENABLE_API ON itself when it wants the binding.
option(BROCONF_ENABLE_API "Build Bronze JavaScript API" ${PROJECT_IS_TOP_LEVEL})
if(BROCONF_ENABLE_API)
    # bronze (with brass, its code generator) through cmake/bro_deps.cmake: an
    # outer project's target, else ../bronze beside the top-level project, else
    # the pinned commit, fetched at configure.
    bro_dependency(bronze GITHUB wlejon/bronze TARGET bronze_runtime_shared
                   OPTIONS BRONZE_BUILD_SHARED_RUNTIME=ON BRONZE_BUILD_TESTS=OFF)

    add_subdirectory(src/api)
endif()

if(BROCONF_BUILD_TESTS)
    add_subdirectory(tests)
endif()

