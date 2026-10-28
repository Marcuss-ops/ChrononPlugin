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
find_package(OpenCV QUIET COMPONENTS core imgproc imgcodecs)
if(OpenCV_FOUND)
    set(_opencv_libs ${OpenCV_LIBS})
    set(_opencv_includes ${OpenCV_INCLUDE_DIRS})
else()
    set(_opencv_prefix "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv-dev/prefix/usr")
    set(_opencv_libdir "${_opencv_prefix}/lib/x86_64-linux-gnu")
    if(NOT EXISTS "${_opencv_libdir}/libopencv_core.a"
       OR NOT EXISTS "${_opencv_libdir}/libopencv_imgproc.a"
       OR NOT EXISTS "${_opencv_libdir}/libopencv_imgcodecs.a")
        message(FATAL_ERROR
            "OpenCV core/imgproc/imgcodecs not found; install full OpenCV")
    endif()
    add_library(chrononplugin_opencv_core STATIC IMPORTED)
    set_target_properties(chrononplugin_opencv_core PROPERTIES
        IMPORTED_LOCATION "${_opencv_libdir}/libopencv_core.a")
    add_library(chrononplugin_opencv_imgproc STATIC IMPORTED)
    set_target_properties(chrononplugin_opencv_imgproc PROPERTIES
        IMPORTED_LOCATION "${_opencv_libdir}/libopencv_imgproc.a")
    add_library(chrononplugin_opencv_imgcodecs STATIC IMPORTED)
    set_target_properties(chrononplugin_opencv_imgcodecs PROPERTIES
        IMPORTED_LOCATION "${_opencv_libdir}/libopencv_imgcodecs.a")
    # The static OpenCV bootstrap was built against the TBB 2 task ABI, which
    # the system libtbb.so.12 (TBB 2021) no longer exports. Link libtbb.so.2.
    find_library(_tbb2_lib NAMES libtbb.so.2)
    if(NOT _tbb2_lib)
        message(FATAL_ERROR "libtbb.so.2 not found; required by the static OpenCV bootstrap")
    endif()
    # The Debian static imgcodecs archive references its codec dependencies
    # directly. This host ships runtime sonames only (no -dev symlinks) for
    # several of them, so match the sonames explicitly and fail closed if one
    # is absent instead of dying at link time with a symbol list.
    find_library(_zlib_lib NAMES z libz.so.1)
    find_library(_jpeg_lib NAMES libjpeg.so.8 jpeg)
    find_library(_png_lib NAMES png libpng16.so.16 libpng.so.16)
    find_library(_webp_lib NAMES libwebp.so.7 webp)
    find_library(_tiff_lib NAMES libtiff.so.5 tiff)
    find_library(_openjp2_lib NAMES libopenjp2.so.7 openjp2)
    find_library(_gdal_lib NAMES libgdal.so.30 gdal PATHS /usr/lib /usr/lib/ogdi/4.1)
    # imgcodecs also carries DICOM (GDCM) and OpenEXR decoders.
    find_library(_exr_lib NAMES libIlmImf-2_5.so.25 IlmImf)
    find_library(_iex_lib NAMES libIex-2_5.so.25 Iex)
    find_library(_ilmthread_lib NAMES libIlmThread-2_5.so.25 IlmThread)
    find_library(_half_lib NAMES libHalf-2_5.so.25 Half)
    find_library(_gdcm_msff_lib NAMES libgdcmMSFF.so.3.0 gdcmMSFF)
    find_library(_gdcm_common_lib NAMES libgdcmCommon.so.3.0 gdcmCommon)
    find_library(_gdcm_dict_lib NAMES libgdcmDICT.so.3.0 gdcmDICT)
    find_library(_gdcm_dsed_lib NAMES libgdcmDSED.so.3.0 gdcmDSED)
    find_library(_gdcm_iod_lib NAMES libgdcmIOD.so.3.0 gdcmIOD)
    foreach(_dep_lib _zlib_lib _jpeg_lib _png_lib _webp_lib _tiff_lib _openjp2_lib _gdal_lib
            _exr_lib _iex_lib _ilmthread_lib _half_lib
            _gdcm_msff_lib _gdcm_common_lib _gdcm_dict_lib _gdcm_dsed_lib _gdcm_iod_lib)
        if(NOT ${_dep_lib})
            message(FATAL_ERROR
                "OpenCV codec dependency ${_dep_lib} not found; install the runtime library")
        endif()
    endforeach()
    set(_opencv_libs chrononplugin_opencv_imgcodecs chrononplugin_opencv_imgproc chrononplugin_opencv_core
        ${_tbb2_lib} ${_zlib_lib} ${_png_lib} ${_jpeg_lib} ${_webp_lib} ${_tiff_lib} ${_openjp2_lib} ${_gdal_lib}
        ${_exr_lib} ${_iex_lib} ${_ilmthread_lib} ${_half_lib}
        ${_gdcm_msff_lib} ${_gdcm_common_lib} ${_gdcm_dict_lib} ${_gdcm_dsed_lib} ${_gdcm_iod_lib})
    set(_opencv_includes
        "${_opencv_prefix}/include/opencv4"
        "${_opencv_prefix}/include/x86_64-linux-gnu/opencv4")
endif()

add_library(chrononplugin_maps STATIC
    src/esri_satellite.cpp
    src/feature_query.cpp
    src/geo_boolean.cpp
    src/geojson.cpp
    src/historical_timeline.cpp
    src/map_camera.cpp
    src/map_label_render.cpp
    src/map_plate_compose.cpp
    src/map_plate_raster.cpp
    src/map_plate_render.cpp
    src/map_renderer_catalog.cpp
    src/map_plate_telemetry.cpp
    src/map_route_render.cpp
    src/map_story_effects.cpp
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

# The published vocabulary must match the renderer implementation. This is the
# gate that keeps maps-renderer-catalog.v1.json honest for the repositories
# that consume it (RenderingGen embeds the same file).
add_executable(maps_renderer_catalog_test tests/map_renderer_catalog_test.cpp)
target_link_libraries(maps_renderer_catalog_test PRIVATE chrononplugin_maps)
target_compile_definitions(maps_renderer_catalog_test PRIVATE
    CHRONONPLUGIN_MAPS_RENDERER_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/maps-renderer-catalog.v1.json")
add_test(NAME maps_renderer_catalog_test COMMAND maps_renderer_catalog_test)

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
# The machine-readable renderer vocabulary travels with the binary: consumers
# read the same catalog the renderer validates against.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/maps-renderer-catalog.v1.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/chrononplugin")
# Only the two boundary sources map_plate_render reads (Natural Earth 50m
# countries + geoBoundaries ITA ADM2): the ChrononTemplate catalog also holds
# plates and fixtures that never belong in a runtime image.
install(FILES "${CHRONON_MAPS_CATALOG_DIR}/ne_50m_admin_0_countries.geojson"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/chrononplugin/maps-catalog")
install(FILES "${CHRONON_MAPS_CATALOG_DIR}/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/chrononplugin/maps-catalog/italy_admin")
install(FILES "${CHRONON_MAPS_CATALOG_DIR}/italy_admin/geoBoundaries_ITA_ADM3.simplified.geojson"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/chrononplugin/maps-catalog/italy_admin")
# The renderer and its default test suite are native C++. Python is only an
# optional reference oracle for cross-implementation parity checks; it is not
# needed to configure, build, or run ChrononPlugin maps.
option(CHRONONPLUGIN_ENABLE_PYTHON_MAP_REFERENCE_TESTS
    "Enable optional Python reference and cartography tests" OFF)
if(CHRONONPLUGIN_ENABLE_PYTHON_MAP_REFERENCE_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(_cartography "${CMAKE_CURRENT_SOURCE_DIR}/tools/cartography")
    set(_map_reference "${CMAKE_CURRENT_SOURCE_DIR}/../../../ChrononTemplate/tools/map")

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

    # Bit-exact checks against the former Python implementation. These are
    # opt-in development oracles; the native renderer has no Python runtime
    # dependency.
    add_test(NAME maps_raster_parity_check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_native/parity_raster_check.py"
            --dump-tool $<TARGET_FILE:maps_raster_parity_dump>
            --python-reference "${_map_reference}"
            --catalog "${CHRONON_MAPS_CATALOG_DIR}"
            --cartography "${_cartography}")
    set_tests_properties(maps_raster_parity_check PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 600)

    add_test(NAME maps_camera_label_parity_check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_native/parity_camera_label_check.py"
            --dump-tool $<TARGET_FILE:maps_camera_label_parity_dump>
            --python-reference "${_map_reference}"
            --cartography "${_cartography}")
    set_tests_properties(maps_camera_label_parity_check PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 600)

    add_test(NAME maps_composite_parity_check
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/map_native/parity_composite_check.py"
            --render-tool $<TARGET_FILE:maps_dynamic_map_render>
            --python-reference "${_map_reference}"
            --cartography "${_cartography}"
            --catalog "${CHRONON_MAPS_CATALOG_DIR}")
    set_tests_properties(maps_composite_parity_check PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 600)
endif()
