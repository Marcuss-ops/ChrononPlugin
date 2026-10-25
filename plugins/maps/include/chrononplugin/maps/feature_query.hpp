#pragma once

#include <string_view>
#include <vector>

#include "chrononplugin/maps/geo_feature.hpp"

namespace chrononplugin::maps {

/// Returns the features whose property `key` equals `value`, in collection order.
/// Pointers refer into `collection` and are valid while it is alive.
[[nodiscard]] std::vector<const GeoFeature*> query_by_property(
    const GeoFeatureCollection& collection, std::string_view key, std::string_view value);

}  // namespace chrononplugin::maps
