#include "chrononplugin/maps/geojson.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

namespace chrononplugin::maps {
namespace {

using json = nlohmann::json;

GeoPosition parse_position(const json& p) {
    if (!p.is_array() || p.size() < 2 || !p[0].is_number() || !p[1].is_number()) {
        throw std::runtime_error("geojson: position must be [lon, lat]");
    }
    return {p[0].get<double>(), p[1].get<double>()};
}

GeoPolygon parse_polygon(const json& rings) {
    if (!rings.is_array() || rings.empty()) {
        throw std::runtime_error("geojson: polygon needs at least one ring");
    }
    GeoPolygon polygon;
    for (const auto& ring : rings) {
        if (!ring.is_array() || ring.size() < 4) {
            throw std::runtime_error("geojson: ring needs at least 4 positions");
        }
        std::vector<GeoPosition> positions;
        positions.reserve(ring.size());
        for (const auto& p : ring) {
            positions.push_back(parse_position(p));
        }
        if (positions.front() != positions.back()) {
            throw std::runtime_error("geojson: ring must be closed");
        }
        polygon.rings.push_back(std::move(positions));
    }
    return polygon;
}

std::string property_to_string(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number()) return std::to_string(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return {};  // null, objects and arrays are not kept as scalar properties
}

GeoFeature parse_feature(const json& feature) {
    if (!feature.is_object() || feature.value("type", "") != "Feature") {
        throw std::runtime_error("geojson: expected a Feature object");
    }
    GeoFeature out;
    if (feature.contains("id")) {
        const auto& id = feature["id"];
        out.id = id.is_string() ? id.get<std::string>() : std::to_string(id.get<long long>());
    }
    if (feature.contains("properties") && feature["properties"].is_object()) {
        for (const auto& [key, value] : feature["properties"].items()) {
            out.properties.emplace(key, property_to_string(value));
        }
    }
    const auto& geometry = feature.at("geometry");
    if (geometry.is_null()) {
        return out;  // a Feature without geometry is kept with no polygons
    }
    const auto type = geometry.value("type", "");
    const auto& coords = geometry.at("coordinates");
    if (type == "Polygon") {
        out.polygons.push_back(parse_polygon(coords));
    } else if (type == "MultiPolygon") {
        if (!coords.is_array()) {
            throw std::runtime_error("geojson: MultiPolygon coordinates must be an array");
        }
        for (const auto& polygon : coords) {
            out.polygons.push_back(parse_polygon(polygon));
        }
    } else {
        throw std::runtime_error("geojson: unsupported geometry type '" + type + "'");
    }
    return out;
}

}  // namespace

GeoFeatureCollection parse_geojson(std::string_view text) {
    json doc;
    try {
        doc = json::parse(text);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("geojson: invalid JSON: ") + e.what());
    }
    GeoFeatureCollection out;
    const auto type = doc.value("type", "");
    if (type == "FeatureCollection") {
        for (const auto& feature : doc.at("features")) {
            out.features.push_back(parse_feature(feature));
        }
    } else if (type == "Feature") {
        out.features.push_back(parse_feature(doc));
    } else {
        throw std::runtime_error("geojson: expected FeatureCollection or Feature");
    }
    return out;
}

}  // namespace chrononplugin::maps
