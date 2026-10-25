cmake_minimum_required(VERSION 3.20)

find_package(nlohmann_json CONFIG REQUIRED)

# Reuse the workspace vcpkg installation when the Clipper2 package is not
# installed system-wide. ChrononPlugin remains independently buildable when
# this workspace is not present, provided Clipper2 is available to CMake.
find_package(Clipper2 CONFIG QUIET)
if(NOT Clipper2_FOUND)
    file(GLOB _clipper2_dirs
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../Chronon3d/vcpkg_installed/*/x64-linux/share/clipper2")
    foreach(_dir IN LISTS _clipper2_dirs)
        set(Clipper2_DIR "${_dir}")
        find_package(Clipper2 CONFIG QUIET)
        if(Clipper2_FOUND)
            break()
        endif()
    endforeach()
endif()
if(NOT Clipper2_FOUND)
    message(FATAL_ERROR "Clipper2 not found; install its CMake package or provide Clipper2_DIR")
endif()

# Prefer a complete system OpenCV package. The bootstrap prefix is deliberately
# header/static-only, so importing its distro CMake config would point at shared
# objects that the development packages do not contain.
find_package(OpenCV QUIET COMPONENTS core imgproc)
if(OpenCV_FOUND)
    set(_opencv_libs ${OpenCV_LIBS})
    set(_opencv_includes ${OpenCV_INCLUDE_DIRS})
else()
    set(_opencv_prefix "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv-dev/prefix/usr")
    set(_opencv_libdir "${_opencv_prefix}/lib/x86_64-linux-gnu")
    if(NOT EXISTS "${_opencv_libdir}/libopencv_core.a"
       OR NOT EXISTS "${_opencv_libdir}/libopencv_imgproc.a")
        message(FATAL_ERROR
            "OpenCV core/imgproc not found; install OpenCV or run tools/bootstrap_opencv_dev.sh")
    endif()
    add_library(chrononplugin_opencv_core STATIC IMPORTED)
    set_target_properties(chrononplugin_opencv_core PROPERTIES
        IMPORTED_LOCATION "${_opencv_libdir}/libopencv_core.a")
    add_library(chrononplugin_opencv_imgproc STATIC IMPORTED)
    set_target_properties(chrononplugin_opencv_imgproc PROPERTIES
        IMPORTED_LOCATION "${_opencv_libdir}/libopencv_imgproc.a")
    set(_opencv_libs chrononplugin_opencv_imgproc chrononplugin_opencv_core)
    set(_opencv_includes
        "${_opencv_prefix}/include/opencv4"
        "${_opencv_prefix}/include/x86_64-linux-gnu/opencv4")
endif()

add_library(chrononplugin_maps STATIC
    src/feature_query.cpp
    src/geo_boolean.cpp
    src/geojson.cpp
    src/historical_timeline.cpp
    src/map_plate_raster.cpp
    src/map_plate_render.cpp
    src/territory_change.cpp
)
target_include_directories(chrononplugin_maps PUBLIC include PRIVATE ${_opencv_includes})
target_link_libraries(chrononplugin_maps PUBLIC
    Clipper2::Clipper2
    nlohmann_json::nlohmann_json
    ${_opencv_libs}
)

add_executable(maps_geo_core_test tests/geo_core_test.cpp)
target_link_libraries(maps_geo_core_test PRIVATE chrononplugin_maps)
add_test(NAME maps_geo_core_test COMMAND maps_geo_core_test)

add_executable(maps_map_plate_render_test tests/map_plate_render_test.cpp)
target_link_libraries(maps_map_plate_render_test PRIVATE chrononplugin_maps)
add_test(NAME maps_map_plate_render_test COMMAND maps_map_plate_render_test)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_cartography "${CMAKE_CURRENT_SOURCE_DIR}/tools/cartography")
add_test(NAME maps_cartography_geodesy
    COMMAND "${Python3_EXECUTABLE}" "${_cartography}/map_cartography.py" --self-test)
set_tests_properties(maps_cartography_geodesy PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME maps_dynamic_tile_pyramid_offline
    COMMAND "${Python3_EXECUTABLE}" "${_cartography}/test_dynamic_tile_pyramid_suite.py" --offline)
set_tests_properties(maps_dynamic_tile_pyramid_offline PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    SKIP_RETURN_CODE 77
    TIMEOUT 300)
