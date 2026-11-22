#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "chrononplugin/maps/geo_feature.hpp"

namespace chrononplugin::maps {

/// Parses a GeoJSON FeatureCollection (or a single Feature) containing
/// Polygon or MultiPolygon geometries. Other geometry types are rejected.
/// Property values are stored as strings (numbers and booleans are converted).
/// Throws std::runtime_error on malformed input.
[[nodiscard]] GeoFeatureCollection parse_geojson(std::string_view text);

/// Process-wide cache for boundary catalogs that are read by every map job.
///
/// The key is the canonical path plus file size and mtime, so a changed file
/// is parsed again and two catalogs can never share an entry. The cache is
/// bounded (16 files) and only retains immutable parsed collections.
struct GeoJsonCacheStats {
    std::uint64_t files_loaded{0};
    std::uint64_t cache_hits{0};
    std::uint64_t invalidations{0};
};

[[nodiscard]] GeoJsonCacheStats geojson_cache_stats();
/// Test hook: drop every cached collection without touching the counters.
void reset_geojson_cache();

/// Reads and parses a GeoJSON file through the process cache. Callers share
/// the returned immutable collection; a modified file (size or mtime) is
/// reloaded on the next call.
[[nodiscard]] std::shared_ptr<const GeoFeatureCollection>
load_geojson_file_cached(const std::string& path);

}  // namespace chrononplugin::maps
