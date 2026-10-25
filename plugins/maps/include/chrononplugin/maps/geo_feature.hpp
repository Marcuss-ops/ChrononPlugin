#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace chrononplugin::maps {

/// A position as {longitude, latitude} in degrees.
using GeoPosition = std::array<double, 2>;

/// Polygon with rings: rings[0] is the exterior, following rings are holes.
struct GeoPolygon {
    std::vector<std::vector<GeoPosition>> rings;
};

/// A GeoJSON feature. Geometry is always a list of polygons (a Polygon is
/// stored as a single-element MultiPolygon).
struct GeoFeature {
    std::string id;
    std::map<std::string, std::string> properties;
    std::vector<GeoPolygon> polygons;
};

/// Immutable-by-convention collection produced by the GeoJSON importer.
struct GeoFeatureCollection {
    std::vector<GeoFeature> features;
};

}  // namespace chrononplugin::maps
