#pragma once

#include <vector>

#include "chrononplugin/maps/geo_feature.hpp"

namespace chrononplugin::maps {

enum class GeoBooleanOp { Union, Difference, Intersection, Xor };

/// Boolean operation between two polygon sets (degrees, lon/lat).
/// Each operand must consist of valid, non-overlapping polygons; holes are
/// preserved in the result. Coordinates are quantized to a fixed grid
/// (about 1e-6 degrees) before clipping, so results are deterministic but not
/// exact to floating-point precision.
[[nodiscard]] std::vector<GeoPolygon> geo_boolean(GeoBooleanOp op,
                                                  const std::vector<GeoPolygon>& subject,
                                                  const std::vector<GeoPolygon>& clip);

/// Merges all polygons into the minimal set of polygons covering their union,
/// removing internal shared edges. Overlapping inputs are allowed.
[[nodiscard]] std::vector<GeoPolygon> geo_dissolve(const std::vector<GeoPolygon>& polygons);

}  // namespace chrononplugin::maps
