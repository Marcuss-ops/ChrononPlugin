set(CHRONON3D_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../Chronon3d"
    CACHE PATH "Chronon3D source tree used by the typography plugin")
if(NOT EXISTS "${CHRONON3D_SOURCE_DIR}/include/chronon3d/text/timed_text_document.hpp")
    message(FATAL_ERROR "CHRONON3D_SOURCE_DIR must point to a Chronon3D checkout")
endif()

add_library(chrononplugin_typography STATIC
    src/subtitle_track_builder.cpp
    src/word_emphasis_animators.cpp
)
target_compile_features(chrononplugin_typography PUBLIC cxx_std_20)
target_include_directories(chrononplugin_typography PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CHRONON3D_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
file(GLOB _chronon3d_dependency_include_dirs
    "${CHRONON3D_SOURCE_DIR}/vcpkg_installed/*/*/include"
    "${CHRONON3D_SOURCE_DIR}/sdk-dist/third_party/*/include")
if(_chronon3d_dependency_include_dirs)
    target_include_directories(chrononplugin_typography PUBLIC
        ${_chronon3d_dependency_include_dirs})
endif()
if(EXISTS "${CHRONON3D_SOURCE_DIR}/sdk-dist/lib/cmake/Chronon3D/Chronon3DConfig.cmake")
    set(Chronon3D_DIR "${CHRONON3D_SOURCE_DIR}/sdk-dist/lib/cmake/Chronon3D")
endif()
find_package(Chronon3D CONFIG QUIET)
if(TARGET Chronon3D::SDK)
    target_link_libraries(chrononplugin_typography PUBLIC Chronon3D::SDK)
endif()

option(CHRONONPLUGIN_BUILD_TYPOGRAPHY_TESTS "Build the extracted typography tests" OFF)
if(CHRONONPLUGIN_BUILD_TYPOGRAPHY_TESTS)
    file(GLOB _chronon3d_vcpkg_prefixes
        "${CHRONON3D_SOURCE_DIR}/vcpkg_installed/*/*")
    list(APPEND CMAKE_PREFIX_PATH ${_chronon3d_vcpkg_prefixes})
    find_package(doctest CONFIG REQUIRED)
    if(NOT TARGET Chronon3D::SDK)
        message(FATAL_ERROR "Typography tests require an installed Chronon3D SDK")
    endif()

    function(chrononplugin_add_typography_test name source)
        add_executable(${name}
            tests/test_main.cpp
            "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        target_include_directories(${name} PRIVATE
            "${CHRONON3D_SOURCE_DIR}"
            "${CHRONON3D_SOURCE_DIR}/include"
            "${CHRONON3D_SOURCE_DIR}/tests"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        target_link_libraries(${name} PRIVATE
            chrononplugin_typography Chronon3D::SDK doctest::doctest)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    chrononplugin_add_typography_test(chrononplugin_text_focus_tests
        "tests/text_focus_resolver.cpp")
    chrononplugin_add_typography_test(chrononplugin_text_motion_tests
        "tests/text_motion_presets.cpp")
    chrononplugin_add_typography_test(chrononplugin_word_emphasis_tests
        "tests/word_emphasis_animators.cpp")
    chrononplugin_add_typography_test(chrononplugin_lightweight_emphasis_tests
        "tests/lightweight_emphasis_animators.cpp")
    chrononplugin_add_typography_test(chrononplugin_subtitle_authoring_tests
        "tests/subtitle_font_ref.cpp")
endif()
