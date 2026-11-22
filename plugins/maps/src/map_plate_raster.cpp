// Chronon3D native vector map-plate rasterization — TICKET-MAPS-NATIVE-PORT
// fase 2. Pixel-parity with the Python renderer (see the header contract):
// every formula mirrors ChrononTemplate/tools/map/vector_plate_pyramid.py in
// the same order of operations. This unit owns the raster lifecycle (source
// parsing, zoom projection cache, persistent tile cache and its budget);
// per-tile rasterization and plate stitching live in
// map_plate_raster_tiles.cpp.

#include "chrononplugin/maps/map_plate_raster.hpp"

#include "map_plate_raster_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdint>
#include <cstddef>

namespace chrononplugin::maps {

using namespace raster_detail;

namespace {
std::atomic<std::size_t> g_raster_geojson_parse_count{0};
}  // namespace

namespace {

// On-disk tile format: a fixed header plus the raw BGR bytes. The checksum is
// what makes a partial/corrupt file a MISS instead of a corrupted frame.
struct DiskTileHeader {
    char magic[4];
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t channels;
    std::uint32_t checksum;
};
static_assert(sizeof(DiskTileHeader) == 20, "disk tile header must stay packed");

[[nodiscard]] std::uint32_t fnv1a32(const unsigned char* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

[[nodiscard]] std::uint64_t fnv1a64(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/// _polygons(): Polygon -> one polygon, MultiPolygon -> its polygons, else none.
/// Only the outer ring (polygon[0]) is drawn, and only with >= 3 positions.
void append_geometry_rings(const nlohmann::json& geometry,
                           std::vector<std::vector<std::pair<double, double>>>& out) {
    const std::string type = geometry.value("type", "");
    std::vector<const nlohmann::json*> polygons;
    if (type == "Polygon" || type == "MultiPolygon") {
        if (!geometry.contains("coordinates")) {
            throw std::runtime_error(type + " geometry without coordinates");
        }
        const auto& coords = geometry.at("coordinates");
        if (type == "Polygon") {
            polygons.push_back(&coords);
        } else {
            for (const auto& polygon : coords) {
                polygons.push_back(&polygon);
            }
        }
    }
    for (const nlohmann::json* polygon : polygons) {
        if (polygon->empty()) continue;
        const auto& ring = polygon->at(0);
        if (ring.size() < 3) continue;
        std::vector<std::pair<double, double>> raw;
        raw.reserve(ring.size());
        for (const auto& position : ring) {
            raw.emplace_back(position.at(0).get<double>(), position.at(1).get<double>());
        }
        out.push_back(std::move(raw));
    }
}

}  // namespace
std::size_t VectorPlateRaster::geojson_parse_count() {
    return g_raster_geojson_parse_count.load(std::memory_order_relaxed);
}

std::vector<VectorPlateRaster::RawRing> VectorPlateRaster::parse_geojson(
    const std::string& path) {
    // Map jobs run back-to-back against the same catalogs. Keep the parsed
    // rings keyed by canonical path + size + mtime so only a changed source is
    // parsed again; the cap keeps a long-lived worker bounded.
    struct CacheEntry {
        std::shared_ptr<const std::vector<RawRing>> rings;
        std::uintmax_t size{0};
        std::filesystem::file_time_type mtime{};
    };
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, CacheEntry> cache;

    std::error_code ec;
    std::string key = std::filesystem::weakly_canonical(path, ec);
    if (ec) key = path;
    const std::uintmax_t size = std::filesystem::file_size(key, ec);
    if (ec) throw std::runtime_error("cannot stat GeoJSON source: " + path);
    const auto mtime = std::filesystem::last_write_time(key, ec);
    if (ec) throw std::runtime_error("cannot stat GeoJSON source: " + path);
    {
        std::lock_guard lock(cache_mutex);
        const auto it = cache.find(key);
        if (it != cache.end() && it->second.size == size &&
            it->second.mtime == mtime) {
            return *it->second.rings;
        }
    }

    std::FILE* f = std::fopen(key.c_str(), "rb");
    if (f == nullptr) {
        throw std::runtime_error("cannot open GeoJSON source: " + path);
    }
    std::string data;
    char buffer[1 << 16];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.append(buffer, got);
    }
    std::fclose(f);

    nlohmann::json doc = nlohmann::json::parse(data);
    if (!doc.contains("features")) {
        throw std::runtime_error("GeoJSON has no features: " + path);
    }
    auto rings = std::make_shared<std::vector<RawRing>>();
    for (const auto& feature : doc.at("features")) {
        // feature.get("geometry") or {}
        const auto it = feature.find("geometry");
        if (it == feature.end() || it->is_null()) continue;
        append_geometry_rings(*it, *rings);
    }
    g_raster_geojson_parse_count.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(cache_mutex);
        if (cache.size() >= 8) cache.clear();
        cache[key] = CacheEntry{rings, size, mtime};
    }
    return *rings;
}

VectorPlateRaster::VectorPlateRaster(VectorMapSources sources, DiskCacheOptions disk)
    : disk_(std::move(disk)) {
    if (sources.countries_geojson.empty() || sources.italy_admin2_geojson.empty()) {
        throw std::runtime_error("both boundary sources are required");
    }
    countries_ = parse_geojson(sources.countries_geojson);
    admin2_ = parse_geojson(sources.italy_admin2_geojson);
    if (countries_.empty()) {
        throw std::runtime_error("countries source yielded no rings: " +
                                 sources.countries_geojson);
    }
    // The persistent key is derived from the geometry the raster actually
    // consumes, so a changed GeoJSON can never reuse another source's tile.
    std::uint64_t digest = 1469598103934665603ull;
    auto feed = [&digest](const std::vector<RawRing>& rings) {
        for (const RawRing& ring : rings) {
            for (const auto& point : ring) {
                digest = fnv1a64(digest, &point.first, sizeof(point.first));
                digest = fnv1a64(digest, &point.second, sizeof(point.second));
            }
        }
    };
    feed(countries_);
    feed(admin2_);
    char key[17] = {};
    std::snprintf(key, sizeof(key), "%016llx", static_cast<unsigned long long>(digest));
    disk_source_key_ = key;
    if (!disk_.dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(disk_.dir, ec);
        if (ec) disk_.dir.clear();  // an unusable cache is disabled, never fatal
    }
}

std::string VectorPlateRaster::tile_disk_path(int z, std::int64_t x, std::int64_t y) const {
    return (std::filesystem::path(disk_.dir) / disk_source_key_ / std::to_string(z) /
            (std::to_string(x) + "_" + std::to_string(y) + ".tile"))
        .string();
}

// load_tile_from_disk returns an empty Mat on any miss, malformed header,
// short read or checksum mismatch: the cache is an optimization and a corrupt
// entry must never become a frame.
cv::Mat VectorPlateRaster::load_tile_from_disk(int z, std::int64_t x, std::int64_t y) const {
    if (disk_.dir.empty()) return {};
    std::ifstream in(tile_disk_path(z, x, y), std::ios::binary);
    if (!in) return {};
    DiskTileHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!in || std::memcmp(header.magic, "CTV1", 4) != 0 || header.width != kTile ||
        header.height != kTile || header.channels != 3) {
        return {};
    }
    const std::size_t bytes = static_cast<std::size_t>(kTile) * kTile * 3;
    std::vector<unsigned char> data(bytes);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(bytes));
    if (!in) return {};
    if (fnv1a32(data.data(), bytes) != header.checksum) return {};
    cv::Mat tile(kTile, kTile, CV_8UC3);
    std::memcpy(tile.data, data.data(), bytes);
    return tile;
}

void VectorPlateRaster::store_tile_to_disk(int z, std::int64_t x, std::int64_t y,
                                           const cv::Mat& tile) const {
    if (disk_.dir.empty() || tile.empty() || tile.type() != CV_8UC3 || tile.cols != kTile ||
        tile.rows != kTile) {
        return;
    }
    const std::string path = tile_disk_path(z, x, y);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) return;
    const std::size_t bytes = static_cast<std::size_t>(kTile) * kTile * 3;
    DiskTileHeader header{};
    std::memcpy(header.magic, "CTV1", 4);
    header.width = kTile;
    header.height = kTile;
    header.channels = 3;
    header.checksum = fnv1a32(tile.data, bytes);
    const std::string tmp = path + ".tmp." + std::to_string(static_cast<long long>(getpid()));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(tile.data), static_cast<std::streamsize>(bytes));
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return;
        }
    }
    // Atomic publish: a concurrent reader sees the old tile or the new one,
    // never a partial write.
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return;
    }
    ++tile_disk_writes_;
    if (disk_.evict_every > 0 && ++disk_writes_since_sweep_ >= disk_.evict_every) {
        disk_writes_since_sweep_ = 0;
        enforce_disk_budget();
    }
}

// enforce_disk_budget keeps the directory under max_bytes, evicting the least
// recently written tiles first. Failures are ignored: losing the cache is a
// lost optimization, never a render failure.
void VectorPlateRaster::enforce_disk_budget() const {
    if (disk_.dir.empty() || disk_.max_bytes == 0) return;
    struct Entry {
        std::string path;
        std::uint64_t size;
        std::filesystem::file_time_type mtime;
    };
    std::vector<Entry> entries;
    std::uint64_t total = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(disk_.dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        const auto size = it->file_size(ec);
        const auto mtime = it->last_write_time(ec);
        if (ec) continue;
        entries.push_back({it->path().string(), size, mtime});
        total += size;
    }
    if (total <= disk_.max_bytes) return;
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
    for (const Entry& entry : entries) {
        if (total <= disk_.max_bytes) break;
        std::error_code remove_ec;
        std::filesystem::remove(entry.path, remove_ec);
        if (!remove_ec) total -= entry.size;
    }
}

const VectorPlateRaster::ZoomData& VectorPlateRaster::zoom_data(int z) const {
    auto it = zoom_cache_.find(z);
    if (it != zoom_cache_.end()) return it->second;

    ZoomData data;
    auto project = [z](const std::vector<RawRing>& raw, std::vector<Ring>& out) {
        out.reserve(raw.size());
        for (const RawRing& raw_ring : raw) {
            std::vector<cv::Point> pts;
            pts.reserve(raw_ring.size());
            double minx = 0, maxx = 0, miny = 0, maxy = 0;
            bool first = true;
            for (const auto& [lon, lat] : raw_ring) {
                const auto [gx, gy] = global_px(lat, lon, z);
                if (first) {
                    minx = maxx = gx;
                    miny = maxy = gy;
                    first = false;
                } else {
                    minx = std::min(minx, gx);
                    maxx = std::max(maxx, gx);
                    miny = std::min(miny, gy);
                    maxy = std::max(maxy, gy);
                }
                // np.round(pts).astype(np.int32)
                pts.emplace_back(round_half_even(gx), round_half_even(gy));
            }
            out.push_back(Ring{minx, maxx, miny, maxy, std::move(pts)});
        }
    };
    project(countries_, data.countries);
    project(admin2_, data.admin2);
    return zoom_cache_.emplace(z, std::move(data)).first->second;
}

std::size_t VectorPlateRaster::ring_count(int z) const {
    const ZoomData& data = zoom_data(z);
    return data.countries.size() + data.admin2.size();
}

}  // namespace chrononplugin::maps
