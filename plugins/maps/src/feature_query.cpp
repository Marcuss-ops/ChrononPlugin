#include "chrononplugin/maps/feature_query.hpp"

namespace chrononplugin::maps {

std::vector<const GeoFeature*> query_by_property(
    const GeoFeatureCollection& collection, std::string_view key, std::string_view value) {
    std::vector<const GeoFeature*> out;
    for (const auto& feature : collection.features) {
        const auto it = feature.properties.find(std::string(key));
        if (it != feature.properties.end() && it->second == value) {
            out.push_back(&feature);
        }
    }
    return out;
}

}  // namespace chrononplugin::maps
