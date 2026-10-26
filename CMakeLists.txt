cmake_minimum_required(VERSION 3.20)
project(ChrononPlugin LANGUAGES CXX)

# Livello C dell'architettura Chronon: catalogo dei plugin specializzati.
# Ogni plugin è una libreria/tool autonomo; il registry canonico e i contratti
# di estensione restano in Chronon3D (ExtensionModule/ExtensionContext).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

option(CHRONONPLUGIN_BUILD_MAPS "Build the maps plugin" ON)
if(CHRONONPLUGIN_BUILD_MAPS)
    add_subdirectory(plugins/maps)
endif()
set(_chrononplugin_effects_default OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Chronon3d/include/chronon3d/render_plan/render_plan.hpp")
    set(_chrononplugin_effects_default ON)
endif()
option(CHRONONPLUGIN_BUILD_EFFECTS "Build the Chronon3D effect recipes plugin"
    ${_chrononplugin_effects_default})
if(CHRONONPLUGIN_BUILD_EFFECTS)
    add_subdirectory(plugins/effects)
endif()
set(_chrononplugin_typography_default OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Chronon3d/include/chronon3d/text/timed_text_document.hpp")
    set(_chrononplugin_typography_default ON)
endif()
option(CHRONONPLUGIN_BUILD_TYPOGRAPHY "Build text authoring and caption recipes" ${_chrononplugin_typography_default})
if(CHRONONPLUGIN_BUILD_TYPOGRAPHY)
    add_subdirectory(plugins/typography)
endif()
