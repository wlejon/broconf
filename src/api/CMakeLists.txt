add_library(broconf_api STATIC
    api.cpp
    host_class.cpp
    native_conf.cpp
)

target_include_directories(broconf_api PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
)

target_link_libraries(broconf_api PUBLIC
    broconf
    bronze_runtime_shared
)

target_compile_features(broconf_api PUBLIC cxx_std_20)

broconf_warnings(broconf_api)
