#pragma once

#include <vector>

#include "chrononplugin/maps/geo_feature.hpp"

namespace chrononplugin::maps {

/// Partition of two territorial states: shared = before ∩ after,
/// gained = after − before, lost = before − after.
struct TerritoryChange {
    std::vector<GeoPolygon> shared;
    std::vector<GeoPolygon> gained;
    std::vector<GeoPolygon> lost;
};

/// Computes the territory change between two polygon sets (see geo_boolean for
/// operand and precision requirements).
[[nodiscard]] TerritoryChange compute_territory_change(const std::vector<GeoPolygon>& before,
                                                       const std::vector<GeoPolygon>& after);

}  // namespace chrononplugin::maps
