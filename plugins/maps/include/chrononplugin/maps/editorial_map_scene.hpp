#pragma once

// EditorialMapScene — the vector bridge between the map domain and the
// template/renderer layers.
//
// The pack in `editorial_map_pack.hpp` owns the raster treatments. This module
// owns the neutral, already-projected scene that a vector consumer can lower to
// a RenderPlan: every ring, hatch segment, pin and label is expressed in
// top-left canvas coordinates, so no GeoJSON (and no projection code) crosses
// into ChrononTemplate or Chronon3D.
//
// Output schema: chrononplugin.editorial-map-scene.v1

#include <string>

#include "chrononplugin/maps/editorial_map_pack.hpp"

namespace chrononplugin::maps {

struct EditorialMapSceneOptions {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int frame_count = 240;

    // Empty strings use the compile-time workspace defaults.
    std::string states_geojson;
    std::string countries_geojson;

    // Editorial content. Defaults reproduce the Indiana / Washington D.C.
    // reference pair used by the raster pack.
    std::string highlight_state = "Indiana";
    std::string title = "INDIANA";
    std::string subtitle = "THE HOOSIER STATE";
    std::string pin_label = "Indianapolis";
    double pin_latitude = 39.7684;
    double pin_longitude = -86.1581;

    // Ramer-Douglas-Peucker tolerance in canvas pixels. The highlight is
    // simplified at this tolerance, context rings at 1.2x.
    double simplify_tolerance = 0.9;
};

struct EditorialMapSceneResult {
    bool ok = false;
    std::string error;
    std::string json;  // chrononplugin.editorial-map-scene.v1 document
};

[[nodiscard]] const char* editorial_map_scene_schema_id() noexcept;

/// Loads the boundary catalogs, projects them with Web Mercator, simplifies
/// and clips the geometry to the canvas, then serializes the scene. The call is
/// fail-closed: malformed catalogs, a missing highlight state or an invalid
/// canvas return ok=false with a diagnostic.
[[nodiscard]] EditorialMapSceneResult build_editorial_map_scene(
    EditorialMapStyle style, const EditorialMapSceneOptions& options);

}  // namespace chrononplugin::maps
