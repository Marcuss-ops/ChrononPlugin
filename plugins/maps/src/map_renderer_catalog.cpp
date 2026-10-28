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
        "", "route_line", "satellite_dive_lock", "crime_scene_radar",
        "multi_leg_flight", "territory_polygon_wipe",
        "cell_tower_triangulation", "isochrone_travel_bubble",
        "timeline_suspect_trail", "thermal_heatmap_overlay",
        "night_vision_infrared", "frontline_fluid_corridor",
        "naval_chokepoint_exclusion", "missile_arc_ballistic",
        "multi_camera_split_matrix", "air_defense_interception_dome",
        "subsea_cable_glow_network", "supply_chain_artery_pulse",
        "great_circle_polar_route", "port_hub_radial_burst",
        "pipeline_energy_flow", "satellite_before_after_split",
        "topographic_contour_rise", "flood_sea_level_inundation",
        "wildfire_burn_scar_expansion", "cinematic_anchor_pitch_tilt",
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
