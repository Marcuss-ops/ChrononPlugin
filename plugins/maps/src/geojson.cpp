#include "chrononplugin/maps/geojson.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

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

// ── process-wide file cache ────────────────────────────────────────────────
// Map jobs run back-to-back on the same catalog, so parsing the boundary
// files per job dominated preparation. Every entry is immutable and keyed by
// canonical path + size + mtime; the cap keeps a long-lived daemon bounded.
constexpr std::size_t kMaxCachedFiles = 16;

struct CacheEntry {
    std::shared_ptr<const GeoFeatureCollection> collection;
    std::uintmax_t size{0};
    std::filesystem::file_time_type mtime{};
};

std::mutex g_cache_mutex;
std::unordered_map<std::string, CacheEntry> g_cache;
GeoJsonCacheStats g_stats{};

std::string read_geojson_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open GeoJSON source: " + path);
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
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

GeoJsonCacheStats geojson_cache_stats() {
    std::lock_guard lock(g_cache_mutex);
    return g_stats;
}

void reset_geojson_cache() {
    std::lock_guard lock(g_cache_mutex);
    g_cache.clear();
}

std::shared_ptr<const GeoFeatureCollection>
load_geojson_file_cached(const std::string& path) {
    std::error_code ec;
    std::string key = std::filesystem::weakly_canonical(path, ec);
    if (ec) key = path;

    std::uintmax_t size = std::filesystem::file_size(key, ec);
    if (ec) {
        throw std::runtime_error("cannot stat GeoJSON source: " + path);
    }
    const auto mtime = std::filesystem::last_write_time(key, ec);
    if (ec) {
        throw std::runtime_error("cannot stat GeoJSON source: " + path);
    }

    {
        std::lock_guard lock(g_cache_mutex);
        const auto it = g_cache.find(key);
        if (it != g_cache.end() && it->second.size == size &&
            it->second.mtime == mtime) {
            ++g_stats.cache_hits;
            return it->second.collection;
        }
    }

    auto parsed = std::make_shared<const GeoFeatureCollection>(
        parse_geojson(read_geojson_file(key)));

    {
        std::lock_guard lock(g_cache_mutex);
        if (g_cache.size() >= kMaxCachedFiles) {
            g_cache.clear();
            ++g_stats.invalidations;
        }
        g_cache[key] = CacheEntry{parsed, size, mtime};
        ++g_stats.files_loaded;
    }
    return parsed;
}

}  // namespace chrononplugin::maps
