cmake_minimum_required(VERSION 3.21)

project(OpenEFB VERSION 0.14.1 LANGUAGES CXX)

option(OPEN_EFB_BUILD_TESTS "Build OpenEFB unit tests" ON)
set(OPEN_EFB_XPLANE_SDK_PATH "" CACHE PATH "Path to the X-Plane SDK root")

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(openefb_core STATIC
    src/core/airspace_model.cpp
    src/core/airport_info.cpp
    src/core/application.cpp
    src/core/briefing_model.cpp
    src/core/flight_plan_model.cpp
    src/core/flight_plan_editor.cpp
    src/core/flight_plan_file.cpp
    src/core/fuel_model.cpp
    src/core/faa_chart_catalog.cpp
    src/core/moving_map_model.cpp
    src/core/navigation_database_model.cpp
    src/core/planning_model.cpp
    src/core/route_progress_model.cpp
    src/core/telemetry_model.cpp
    src/core/ui_model.cpp
    src/core/weather_model.cpp
    src/core/window_controller.cpp
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
    add_library(openefb_plugin MODULE
        src/xplane/plugin.cpp
        src/xplane/xplane_airspace.cpp
        src/xplane/xplane_airport_data.cpp
        src/xplane/xplane_briefing_library.cpp
        src/xplane/xplane_flight_plan.cpp
        src/xplane/xplane_fuel.cpp
        src/xplane/xplane_menu.cpp
        src/xplane/xplane_map_tiles.cpp
        src/xplane/xplane_navigation_database.cpp
        src/xplane/xplane_pdf_viewer.cpp
        src/xplane/xplane_planning.cpp
        src/xplane/xplane_preferences.cpp
        src/xplane/xplane_route_progress.cpp
        src/xplane/xplane_telemetry.cpp
        src/xplane/xplane_weather.cpp
        src/xplane/xplane_window.cpp
    )
    target_link_libraries(openefb_plugin PRIVATE OpenEFB::core)
    target_include_directories(openefb_plugin PRIVATE
        "${OPEN_EFB_XPLANE_SDK_PATH}/CHeaders/XPLM"
        "${OPEN_EFB_XPLANE_SDK_PATH}/CHeaders/Widgets"
    )
    target_compile_features(openefb_plugin PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(openefb_plugin PRIVATE /W4 /permissive-)
    else()
        target_compile_options(openefb_plugin PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32)
        set(OPEN_EFB_PLUGIN_NAME win)
        target_compile_definitions(openefb_plugin PRIVATE
            IBM=1 NOMINMAX=1
            XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1
        )
        target_link_directories(openefb_plugin PRIVATE
            "${OPEN_EFB_XPLANE_SDK_PATH}/Libraries/Win"
        )
        target_link_libraries(openefb_plugin PRIVATE XPLM_64)
        target_link_libraries(openefb_plugin PRIVATE opengl32 windowscodecs winhttp shell32 windowsapp)
    elseif(APPLE)
        set(OPEN_EFB_PLUGIN_NAME mac)
        target_compile_definitions(openefb_plugin PRIVATE
            APL=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1
        )
        target_link_libraries(openefb_plugin PRIVATE
            "${OPEN_EFB_XPLANE_SDK_PATH}/Libraries/Mac/XPLM.framework"
        )
        find_library(OPEN_EFB_OPENGL_FRAMEWORK OpenGL REQUIRED)
        target_link_libraries(openefb_plugin PRIVATE "${OPEN_EFB_OPENGL_FRAMEWORK}")
    elseif(UNIX)
        set(OPEN_EFB_PLUGIN_NAME lin)
        target_compile_definitions(openefb_plugin PRIVATE
            LIN=1 XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1
        )
        find_package(OpenGL REQUIRED)
        target_link_libraries(openefb_plugin PRIVATE OpenGL::GL)
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
