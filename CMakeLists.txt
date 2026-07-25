cmake_minimum_required(VERSION 3.21)

project(OpenEFB VERSION 0.1.0 LANGUAGES CXX)

option(OPEN_EFB_BUILD_TESTS "Build OpenEFB unit tests" ON)
set(OPEN_EFB_XPLANE_SDK_PATH "" CACHE PATH "Path to the X-Plane SDK root")

add_library(openefb_core STATIC
    src/core/application.cpp
)
add_library(OpenEFB::core ALIAS openefb_core)

target_include_directories(openefb_core PUBLIC include)
target_compile_features(openefb_core PUBLIC cxx_std_20)
set_target_properties(openefb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(openefb_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(openefb_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPEN_EFB_XPLANE_SDK_PATH)
    add_library(openefb_plugin MODULE src/xplane/plugin.cpp)
    target_link_libraries(openefb_plugin PRIVATE OpenEFB::core)
    target_include_directories(openefb_plugin PRIVATE
        "${OPEN_EFB_XPLANE_SDK_PATH}/CHeaders/XPLM"
        "${OPEN_EFB_XPLANE_SDK_PATH}/CHeaders/Widgets"
    )
    target_compile_features(openefb_plugin PRIVATE cxx_std_20)

    if(WIN32)
        set(OPEN_EFB_PLUGIN_NAME win)
        target_compile_definitions(openefb_plugin PRIVATE IBM=1 XPLM303=1)
        target_link_directories(openefb_plugin PRIVATE
            "${OPEN_EFB_XPLANE_SDK_PATH}/Libraries/Win"
        )
        target_link_libraries(openefb_plugin PRIVATE XPLM_64)
    elseif(APPLE)
        set(OPEN_EFB_PLUGIN_NAME mac)
        target_compile_definitions(openefb_plugin PRIVATE APL=1 XPLM303=1)
        target_link_libraries(openefb_plugin PRIVATE
            "${OPEN_EFB_XPLANE_SDK_PATH}/Libraries/Mac/XPLM.framework"
        )
    elseif(UNIX)
        set(OPEN_EFB_PLUGIN_NAME lin)
        target_compile_definitions(openefb_plugin PRIVATE LIN=1 XPLM303=1)
    else()
        message(FATAL_ERROR "Unsupported platform for the X-Plane plugin")
    endif()

    set_target_properties(openefb_plugin PROPERTIES
        PREFIX ""
        SUFFIX ".xpl"
        OUTPUT_NAME "${OPEN_EFB_PLUGIN_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/OpenEFB/64"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/OpenEFB/64"
    )
else()
    message(STATUS "OPEN_EFB_XPLANE_SDK_PATH is not set; skipping the .xpl target")
endif()

if(OPEN_EFB_BUILD_TESTS)
    include(CTest)
    add_executable(openefb_core_tests tests/application_tests.cpp)
    target_link_libraries(openefb_core_tests PRIVATE OpenEFB::core)
    target_compile_features(openefb_core_tests PRIVATE cxx_std_20)
    add_test(NAME openefb_core_tests COMMAND openefb_core_tests)
endif()
