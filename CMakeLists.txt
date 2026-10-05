cmake_minimum_required(VERSION 3.24)

project(broconf VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_broconf_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_broconf_top_level ON)
endif()

option(BROCONF_BUILD_TESTS "Build the broconf tests" ${_broconf_top_level})

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
    target_link_libraries(broconf PRIVATE advapi32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(broconf
        PUBLIC
            Threads::Threads
        PRIVATE
            PkgConfig::BROCONF_SYSTEMD)
elseif(APPLE)
    target_link_libraries(broconf
        PUBLIC
            Threads::Threads)
endif()

if(BROCONF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
