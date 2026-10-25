#pragma once

#include <string_view>

#include "chrononplugin/maps/geo_feature.hpp"

namespace chrononplugin::maps {

/// Parses a GeoJSON FeatureCollection (or a single Feature) containing
/// Polygon or MultiPolygon geometries. Other geometry types are rejected.
/// Property values are stored as strings (numbers and booleans are converted).
/// Throws std::runtime_error on malformed input.
[[nodiscard]] GeoFeatureCollection parse_geojson(std::string_view text);

}  // namespace chrononplugin::maps
