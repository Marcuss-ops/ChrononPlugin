#include "chrononplugin/maps/territory_change.hpp"

#include "chrononplugin/maps/geo_boolean.hpp"

namespace chrononplugin::maps {

TerritoryChange compute_territory_change(const std::vector<GeoPolygon>& before,
                                         const std::vector<GeoPolygon>& after) {
    TerritoryChange out;
    out.shared = geo_boolean(GeoBooleanOp::Intersection, before, after);
    out.gained = geo_boolean(GeoBooleanOp::Difference, after, before);
    out.lost = geo_boolean(GeoBooleanOp::Difference, before, after);
    return out;
}

}  // namespace chrononplugin::maps
