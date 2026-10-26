cmake_minimum_required(VERSION 3.20)
project(ChrononPluginEffects LANGUAGES CXX)

add_library(chrononplugin_effects INTERFACE)
target_compile_features(chrononplugin_effects INTERFACE cxx_std_20)
target_include_directories(chrononplugin_effects INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# RenderPlan's C++ authoring headers are currently a source-level API, not part
# of the installed SDK header set. Keep this dependency one-way: the plugin
# consumes those contracts and never gets linked into Chronon3D itself.
set(CHRONON3D_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../Chronon3d"
    CACHE PATH "Chronon3D source tree used by the effects plugin")
if(NOT EXISTS "${CHRONON3D_SOURCE_DIR}/include/chronon3d/render_plan/render_plan.hpp")
    message(FATAL_ERROR "CHRONON3D_SOURCE_DIR must point to a Chronon3D checkout")
endif()
target_include_directories(chrononplugin_effects INTERFACE
    "${CHRONON3D_SOURCE_DIR}/include")
find_package(Chronon3D CONFIG QUIET)
if(TARGET Chronon3D::SDK)
    target_link_libraries(chrononplugin_effects INTERFACE Chronon3D::SDK)
endif()
