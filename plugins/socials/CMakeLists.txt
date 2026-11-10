cmake_minimum_required(VERSION 3.20)
project(ChrononPluginSocials LANGUAGES CXX)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(chrononplugin_socials STATIC
    src/social_showcase.cpp
)
target_compile_features(chrononplugin_socials PUBLIC cxx_std_20)
target_include_directories(chrononplugin_socials PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(chrononplugin_socials PUBLIC nlohmann_json::nlohmann_json)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chrononplugin_socials PRIVATE -Wall -Wextra)
endif()

# C++ authoring CLI: emits plans/series.json. It never renders or uploads;
# tools/render_socials_showcase.py owns those orchestration steps.
add_executable(chrononplugin_emit_socials_showcase
    tools/emit_socials_showcase.cpp
)
target_link_libraries(chrononplugin_emit_socials_showcase
    PRIVATE chrononplugin_socials)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chrononplugin_emit_socials_showcase PRIVATE -Wall -Wextra)
endif()

add_executable(chrononplugin_socials_tests tests/social_showcase_test.cpp)
target_link_libraries(chrononplugin_socials_tests PRIVATE chrononplugin_socials)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(chrononplugin_socials_tests PRIVATE -Wall -Wextra)
endif()
add_test(NAME chrononplugin_socials_tests COMMAND chrononplugin_socials_tests)
