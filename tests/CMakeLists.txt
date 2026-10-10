# broconf_test(<name> SOURCES ... [LIBS ...] [TIMEOUT s]): one real ctest.
# Exit code 77 is a skip (a service genuinely absent, reason printed),
# anything else nonzero a failure.
function(broconf_test name)
    cmake_parse_arguments(T "" "TIMEOUT" "SOURCES;LIBS" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE broconf ${T_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
    broconf_warnings(${name})
    if(WIN32)
        target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
    endif()
    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 60)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT} SKIP_RETURN_CODE 77)
endfunction()

broconf_test(test_types SOURCES test_types.cpp)
broconf_test(test_schema SOURCES test_schema.cpp)
broconf_test(test_keyfile SOURCES test_keyfile.cpp)
broconf_test(test_storage_layered SOURCES test_storage_layered.cpp)
broconf_test(test_store_api SOURCES test_store_api.cpp)
# Deferred, coalesced, atomic writes; reload keeps unwritten changes.
broconf_test(test_persist SOURCES test_persist.cpp)
# A second process writes the settings file; this one's watcher must see it.
broconf_test(test_file_watcher SOURCES test_file_watcher.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Private dbus-daemon; a second process and gdbus emit the signal.
    broconf_test(test_dbus_sync SOURCES test_dbus_sync.cpp)
endif()

if(BROCONF_ENABLE_API)
    add_executable(broconf_test_api
        test_conf_api.cpp
    )
    target_include_directories(broconf_test_api PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/api
        ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(broconf_test_api PRIVATE
        broconf_api
        broconf
        bronze_runtime_shared
    )
    broconf_warnings(broconf_test_api)
    if(WIN32)
        target_compile_definitions(broconf_test_api PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
        add_custom_command(TARGET broconf_test_api POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:broconf_test_api>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_LINKER_FILE:bronze_runtime_shared>
                $<TARGET_FILE_DIR:broconf_test_api>
        )
    endif()

    if(NOT BRASS_HOST_BACKEND)
        message(STATUS "broconf: broconf_test_api not registered (brass has no code generator for this target)")
    else()
        add_test(NAME broconf_test_api COMMAND broconf_test_api)
        set_tests_properties(broconf_test_api PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

        add_test(NAME broconf_test_api_gc_stress COMMAND broconf_test_api)
        set_tests_properties(broconf_test_api_gc_stress PROPERTIES
            TIMEOUT 60
            SKIP_RETURN_CODE 77
            ENVIRONMENT "BRONZE_GC_STRESS=1;BRONZE_GC_POISON=1"
        )
        if(WIN32)
            set_tests_properties(broconf_test_api PROPERTIES
                ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:broconf_test_api>;$ENV{PATH}")
            set_property(TEST broconf_test_api_gc_stress APPEND PROPERTY
                ENVIRONMENT "PATH=$<TARGET_FILE_DIR:bronze_runtime_shared>;$<TARGET_FILE_DIR:broconf_test_api>;$ENV{PATH}")
        endif()
    endif()
endif()

# Every test executable links no_dialogs.cpp (see the file).
if(WIN32)
    get_property(_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _test_targets)
        get_target_property(_type ${_t} TYPE)
        if(_type STREQUAL "EXECUTABLE")
            target_sources(${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/no_dialogs.cpp)
        endif()
    endforeach()
endif()

