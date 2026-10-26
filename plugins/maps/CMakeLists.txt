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
    # The static OpenCV bootstrap was built against the TBB 2 task ABI, which
    # the system libtbb.so.12 (TBB 2021) no longer exports. Link libtbb.so.2.
    find_library(_tbb2_lib NAMES libtbb.so.2)
    if(NOT _tbb2_lib)
        message(FATAL_ERROR "libtbb.so.2 not found; required by the static OpenCV bootstrap")
    endif()
    set(_opencv_libs chrononplugin_opencv_imgproc chrononplugin_opencv_core ${_tbb2_lib})
    set(_opencv_includes
        "${_opencv_prefix}/include/opencv4"
        "${_opencv_prefix}/include/x86_64-linux-gnu/opencv4")
endif()

add_library(chrononplugin_maps STATIC
    src/feature_query.cpp
    src/geo_boolean.cpp
    src/geojson.cpp
    src/historical_timeline.cpp
    src/map_camera.cpp
    src/map_label_render.cpp
    src/map_plate_compose.cpp
    src/map_plate_raster.cpp
    src/map_plate_render.cpp
    src/map_plate_telemetry.cpp
    src/territory_change.cpp
)

# Default boundary catalog for the native renderer (overridable at runtime
# with CHRONON_MAPS_CATALOG_DIR).
if(NOT DEFINED CHRONON_MAPS_CATALOG_DIR)
    set(CHRONON_MAPS_CATALOG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../ChrononTemplate/catalog")
endif()
target_compile_definitions(chrononplugin_maps PRIVATE
    CHRONON_MAPS_CATALOG_DIR_DEFAULT="${CHRONON_MAPS_CATALOG_DIR}")

# Bake the native renderer fingerprint from the renderer sources: the cache
# identity must change whenever the renderer code changes
# (TICKET-MAPS-NATIVE-PORT section 4.2).
file(GLOB _maps_renderer_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/chrononplugin/maps/*.hpp")
set(_maps_fingerprint_input "")
foreach(_src IN LISTS _maps_renderer_sources)
    file(READ "${_src}" _maps_fingerprint_content)
    string(APPEND _maps_fingerprint_input "${_maps_fingerprint_content}")
endforeach()
string(SHA256 _maps_fingerprint_hash "${_maps_fingerprint_input}")
string(SUBSTRING "${_maps_fingerprint_hash}" 0 16 _maps_fingerprint_short)
message(STATUS "ChrononPlugin maps renderer fingerprint: maps-native-${_maps_fingerprint_short}")
target_compile_definitions(chrononplugin_maps PRIVATE
    CHRONONPLUGIN_MAPS_RENDERER_FINGERPRINT="maps-native-${_maps_fingerprint_short}")
# OpenCV headers are part of the public raster contract (map_plate_raster.hpp).
target_include_directories(chrononplugin_maps PUBLIC include ${_opencv_includes})
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

# Boundary sources for the raster test: the same files the Python renderer
# reads, from the ChrononTemplate catalog next to this workspace.
add_executable(maps_map_plate_raster_test tests/map_plate_raster_test.cpp)
target_link_libraries(maps_map_plate_raster_test PRIVATE chrononplugin_maps)
target_compile_definitions(maps_map_plate_raster_test PRIVATE
    CHRONON_MAPS_COUNTRIES_GEOJSON="${CHRONON_MAPS_CATALOG_DIR}/ne_50m_admin_0_countries.geojson"
    CHRONON_MAPS_ITALY_ADMIN2_GEOJSON="${CHRONON_MAPS_CATALOG_DIR}/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson")
add_test(NAME maps_map_plate_raster_test COMMAND maps_map_plate_raster_test)

add_executable(maps_camera_label_test tests/map_camera_label_test.cpp)
target_link_libraries(maps_camera_label_test PRIVATE chrononplugin_maps)
add_test(NAME maps_camera_label_test COMMAND maps_camera_label_test)

add_executable(maps_map_plate_telemetry_test tests/map_plate_telemetry_test.cpp)
target_link_libraries(maps_map_plate_telemetry_test PRIVATE chrononplugin_maps)
add_test(NAME maps_map_plate_telemetry_test COMMAND maps_map_plate_telemetry_test)


# Parity dump tool for the native rasterizer (fase 2, TICKET-MAPS-NATIVE-PORT):
# tools/map_native/parity_raster_check.py compares its output byte-for-byte
# with the Python reference renderer.
add_executable(maps_raster_parity_dump tools/map_native/raster_parity_dump.cpp)
target_link_libraries(maps_raster_parity_dump PRIVATE chrononplugin_maps)

# Parity dump tool for the camera/motion and label primitives (fasi 3-4).
add_executable(maps_camera_label_parity_dump tools/map_native/camera_label_parity_dump.cpp)
target_link_libraries(maps_camera_label_parity_dump PRIVATE chrononplugin_maps)

# Native dynamic-map renderer CLI (drop-in contract of the retired Python
# renderer: --input/--output/--summary-output).
add_executable(maps_dynamic_map_render tools/map_native/map_render_cli.cpp)
target_link_libraries(maps_dynamic_map_render PRIVATE chrononplugin_maps)

# Distribution (TICKET-MAPS-NATIVE-PORT fase 6): the renderer binary and its
# boundary catalog install as ONE unit. The host drop-in
# refactored/scripts/systemd/pipelinegen.service.d/map-native.conf points at
# these paths (bin/maps_dynamic_map_render + share/chrononplugin/maps-catalog);
# the runtime container image copies the same layout under its prefix.
include(GNUInstallDirs)
install(TARGETS maps_dynamic_map_render
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Only the two boundary sources map_plate_render reads (Natural Earth 50m
# countries + geoBoundaries ITA ADM2): the ChrononTemplate catalog also holds
# plates and fixtures that never belong in a runtime image.
install(FILES "${CHRONON_MAPS_CATALOG_DIR}/ne_50m_admin_0_countries.geojson"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/chrononplugin/maps-catalog")
install(FILES "${CHRONON_MAPS_CATALOG_DIR}/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/chrononplugin/maps-catalog/italy_admin")

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

# Bit-exact parity of the native rasterizer against the Python reference.
# Skips (77) when the reference renderer or the boundary catalog is absent,
# so the plugin stays independently buildable.
add_test(NAME maps_raster_parity_check
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_native/parity_raster_check.py"
        --dump-tool $<TARGET_FILE:maps_raster_parity_dump>
        --python-reference "${CMAKE_CURRENT_SOURCE_DIR}/../../../ChrononTemplate/tools/map"
        --catalog "${CHRONON_MAPS_CATALOG_DIR}"
        --cartography "${CMAKE_CURRENT_SOURCE_DIR}/tools/cartography")
set_tests_properties(maps_raster_parity_check PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 600)

# Bit-exact parity of camera/motion and label primitives against the Python
# reference closures. Skips (77) when the reference modules are absent.
add_test(NAME maps_camera_label_parity_check
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_native/parity_camera_label_check.py"
        --dump-tool $<TARGET_FILE:maps_camera_label_parity_dump>
        --python-reference "${CMAKE_CURRENT_SOURCE_DIR}/../../../ChrononTemplate/tools/map"
        --cartography "${CMAKE_CURRENT_SOURCE_DIR}/tools/cartography")
set_tests_properties(maps_camera_label_parity_check PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 600)

# Bit-exact composite parity: native flyover frames vs the Python product
# functions (shot builder + sampler + labels). Skips (77) when the reference
# modules are absent.
add_test(NAME maps_composite_parity_check
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_native/parity_composite_check.py"
        --render-tool $<TARGET_FILE:maps_dynamic_map_render>
        --python-reference "${CMAKE_CURRENT_SOURCE_DIR}/../../../ChrononTemplate/tools/map"
        --cartography "${CMAKE_CURRENT_SOURCE_DIR}/tools/cartography"
        --catalog "${CHRONON_MAPS_CATALOG_DIR}")
set_tests_properties(maps_composite_parity_check PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 600)
