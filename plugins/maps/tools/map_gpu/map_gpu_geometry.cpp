// Projected map-shape loading and 2.5D geometry extrusion for the map_gpu tool.
// Split out of map_gpu_render.cpp.

#include "map_gpu_geometry.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace chrononplugin::maps::gpu_detail {

bool point_in_ring(const std::vector<Point2>& ring, float x, float z) {
    bool inside = false;
    if (ring.size() < 3) return false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Point2& a = ring[i];
        const Point2& b = ring[j];
        const bool crosses = ((a.z > z) != (b.z > z)) &&
            (x < (b.x - a.x) * (z - a.z) / ((b.z - a.z) + 1e-12f) + a.x);
        if (crosses) inside = !inside;
    }
    return inside;
}

bool shape_contains(const MapShape& shape, float x, float z) {
    for (const auto& ring : shape.rings) {
        if (point_in_ring(ring, x, z)) return true;
    }
    return false;
}

MapShape load_shape(const std::string& path, const std::string& feature_name,
                    std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "cannot open GeoJSON: " + path;
        return {};
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const std::exception& exc) {
        error = std::string("cannot parse GeoJSON: ") + exc.what();
        return {};
    }
    const auto features = document.find("features");
    if (features == document.end() || !features->is_array()) {
        error = "GeoJSON has no FeatureCollection features";
        return {};
    }

    std::vector<std::vector<std::pair<double, double>>> raw_rings;
    double min_lon = 1e30, max_lon = -1e30, min_lat = 1e30, max_lat = -1e30;
    for (const auto& feature : *features) {
        if (!feature.is_object()) continue;
        const auto properties = feature.find("properties");
        if (properties == feature.end() ||
            properties->value("shapeName", "") != feature_name) {
            continue;
        }
        const auto geometry = feature.find("geometry");
        if (geometry == feature.end() || !geometry->is_object()) continue;
        const std::string type = geometry->value("type", "");
        const auto coordinates = geometry->find("coordinates");
        if (coordinates == geometry->end()) continue;

        auto add_polygon = [&](const nlohmann::json& polygon) {
            if (!polygon.is_array() || polygon.empty()) return;
            const auto& outer = polygon.at(0);
            if (!outer.is_array() || outer.size() < 3) return;
            std::vector<std::pair<double, double>> ring;
            ring.reserve(outer.size());
            for (const auto& point : outer) {
                if (!point.is_array() || point.size() < 2) continue;
                const double lon = point.at(0).get<double>();
                const double lat = point.at(1).get<double>();
                ring.emplace_back(lon, lat);
                min_lon = std::min(min_lon, lon);
                max_lon = std::max(max_lon, lon);
                min_lat = std::min(min_lat, lat);
                max_lat = std::max(max_lat, lat);
            }
            if (ring.size() >= 3) raw_rings.push_back(std::move(ring));
        };

        if (type == "Polygon") {
            add_polygon(*coordinates);
        } else if (type == "MultiPolygon") {
            for (const auto& polygon : *coordinates) add_polygon(polygon);
        }
    }
    if (raw_rings.empty()) {
        error = "territory feature not found in GeoJSON: " + feature_name;
        return {};
    }

    const double center_lon = 0.5 * (min_lon + max_lon);
    const double center_lat = 0.5 * (min_lat + max_lat);
    const double cos_lat = std::cos(center_lat * kPi / 180.0);
    const double width_m = (max_lon - min_lon) * 111320.0 * cos_lat;
    const double height_m = (max_lat - min_lat) * 110540.0;
    const double scale = 1.9 / std::max(1.0, std::max(width_m, height_m));

    MapShape shape;
    shape.min_x = 1e30f;
    shape.max_x = -1e30f;
    shape.min_z = 1e30f;
    shape.max_z = -1e30f;
    for (const auto& raw : raw_rings) {
        std::vector<Point2> ring;
        ring.reserve(raw.size());
        for (const auto& point : raw) {
            const float x = static_cast<float>(
                (point.first - center_lon) * 111320.0 * cos_lat * scale);
            const float z = static_cast<float>(
                -(point.second - center_lat) * 110540.0 * scale);
            ring.push_back({x, z});
            shape.min_x = std::min(shape.min_x, x);
            shape.max_x = std::max(shape.max_x, x);
            shape.min_z = std::min(shape.min_z, z);
            shape.max_z = std::max(shape.max_z, z);
        }
        shape.rings.push_back(std::move(ring));
    }
    error.clear();
    return shape;
}

float hash01(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffU) /
           static_cast<float>(0x00ffffffU);
}

Geometry build_geometry(const MapShape& shape) {
    Geometry geometry;
    const int grid_x = 96;
    const int grid_z = 64;
    const float cell_x = (shape.max_x - shape.min_x) / grid_x;
    const float cell_z = (shape.max_z - shape.min_z) / grid_z;
    geometry.outline = {};
    for (const auto& ring : shape.rings) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Point2& a = ring[i];
            const Point2& b = ring[(i + 1) % ring.size()];
            geometry.outline.push_back({a.x, 0.016f, a.z});
            geometry.outline.push_back({b.x, 0.016f, b.z});
        }
    }

    for (int iz = 0; iz < grid_z; ++iz) {
        for (int ix = 0; ix < grid_x; ++ix) {
            const float x = shape.min_x + (ix + 0.5f) * cell_x;
            const float z = shape.min_z + (iz + 0.5f) * cell_z;
            if (!shape_contains(shape, x, z)) continue;
            const float noise = hash01(static_cast<std::uint32_t>(ix) * 0x9e3779b9U ^
                                       static_cast<std::uint32_t>(iz) * 0x85ebca6bU);
            const float cluster = 0.5f + 0.5f * std::sin(
                x * 9.0f + z * 5.0f + noise * 2.0f);
            const float height = 0.018f + 0.20f *
                std::pow(noise, 1.7f) * (0.58f + 0.42f * cluster);

            geometry.plates.push_back(
                {x, z, cell_x * 0.47f, cell_z * 0.47f, 0.006f,
                 0.02f, 0.20f, 0.24f});
            if (noise > 0.06f) {
                const float t = std::clamp((height - 0.02f) / 0.22f, 0.0f, 1.0f);
                geometry.towers.push_back(
                    {x, z, cell_x * 0.33f, cell_z * 0.33f, height,
                     0.07f + 0.83f * t, 0.52f + 0.30f * t, 0.92f - 0.64f * t});
                geometry.max_height = std::max(geometry.max_height, height);
            }
        }
    }
    geometry.max_height = std::max(0.08f, geometry.max_height);
    return geometry;
}

}  // namespace chrononplugin::maps::gpu_detail
