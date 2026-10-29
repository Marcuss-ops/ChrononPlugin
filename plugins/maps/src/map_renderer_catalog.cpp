// Published map-renderer vocabulary (see map_plate_render.hpp).
//
// This translation unit deliberately has no OpenCV, nlohmann_json or renderer
// dependency: it is the small, stable contract surface the catalog gate and
// downstream consumers can link against. `map_plate_render.cpp` validates
// against these same accessors, so there is exactly one C++ owner of the
// vocabulary.

#include "chrononplugin/maps/map_plate_render.hpp"

#include <set>
#include <string>

namespace chrononplugin::maps {

const std::set<std::string>& supported_map_animations() {
    static const std::set<std::string> kAnimations = {
        "", "route_line", "multi_leg_flight", "territory_glow_reveal",
        "area_glow_pulse",
    };
    return kAnimations;
}

const std::set<std::string>& supported_map_camera_animations() {
    static const std::set<std::string> kCameraAnimations = {
        "slow_approach", "exponential_dive"};
    return kCameraAnimations;
}

const std::set<std::string>& supported_map_label_animations() {
    static const std::set<std::string> kLabelAnimations = {"none"};
    return kLabelAnimations;
}

const std::set<std::string>& supported_map_basemaps() {
    static const std::set<std::string> kBasemaps = {
        "chronon_editorial", "esri_sat"};
    return kBasemaps;
}

}  // namespace chrononplugin::maps
