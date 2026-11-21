#pragma once

// Chronon3D native vector map-plate rasterization — TICKET-MAPS-NATIVE-PORT
// fase 2 («primitiva tile/plate»).
//
// PARITY CONTRACT (bit-exact, verified by tools/map_native/parity_raster_check.py):
// every geometric formula, rounding rule and OpenCV call mirrors
// ChrononTemplate/tools/map/vector_plate_pyramid.py and the plate stitcher
// FastPlateSampler._compose_px, in the same order of operations, so a tile or
// plate produced here is byte-identical to the Python renderer's. In detail:
//
//  * projections use the same double expressions (libm is shared with CPython);
//  * "round to int" is Python's round(): half-to-even -> std::nearbyint;
//  * polygons are rasterized ONE RING PER CALL with cv::LINE_AA, graticule
//    under the geography, land fill then borders then ISTAT province lines —
//    the same draw order and the same per-tile clipping as the Python tile
//    renderer (seam anti-aliasing is part of the reference pixels);
//  * plates are stitched from 256x256 tiles with the same rounding and the
//    same min() slice guard, never re-rasterized as one big canvas.
//
// Nothing here fabricates data: missing/short rings are skipped exactly as the
// Python renderer skips them, and the raster is a pure function of the source
// GeoJSON bytes plus (z, x, y) / plate window.

#include <cstdint>
#include <map>
#include <tuple>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

namespace chrononplugin::maps {

/// Boundary sources, byte-for-byte the ones the Python renderer consumes
/// (Natural Earth 50m Admin 0 countries + geoBoundaries ITA ADM2 simplified).
struct VectorMapSources {
    std::string countries_geojson;
    std::string italy_admin2_geojson;
};

/// Optional on-disk tile cache (ELIM-05). Tiles are immutable and a pure
/// function of (source bytes, z, x, y), so the key is correct by construction;
/// the persistent copy only avoids re-rasterizing the same area in a later
/// render. Empty dir = disabled (the historical, per-render behaviour).
struct DiskCacheOptions {
    std::string dir;
    /// Hard bound on the cache directory. Eviction is LRU by mtime.
    std::uint64_t max_bytes = 2ull * 1024 * 1024 * 1024;  // 2 GiB
    /// How many writes between budget sweeps. 1 = sweep every write (tests).
    std::uint32_t evict_every = 64;
};

class VectorPlateRaster {
  public:
    explicit VectorPlateRaster(VectorMapSources sources, DiskCacheOptions disk = {});

    /// One 256x256 BGR paper tile, mirroring render_vector_tile(z, x, y).
    /// x is wrapped like Python's modulo, y outside the pyramid yields the
    /// blank sea tile — the same contract as VectorPlatePyramid.
    [[nodiscard]] cv::Mat render_tile(int z, std::int64_t x, std::int64_t y) const;

    /// Stitch tiles into a plate window, mirroring FastPlateSampler._compose_px.
    [[nodiscard]] cv::Mat compose_plate(double left, double top,
                                        int width, int height, int zoom) const;

    /// Number of outer rings carried at a zoom (diagnostics only).
    [[nodiscard]] std::size_t ring_count(int z) const;

    /// Actual local tile rasterizations and cache hits during plate composition.
    [[nodiscard]] std::int64_t tile_cache_hits() const { return tile_cache_hits_; }
    [[nodiscard]] std::int64_t tile_cache_misses() const { return tile_cache_misses_; }
    /// Tiles served from the PERSISTENT cache instead of being rasterized.
    [[nodiscard]] std::int64_t tile_disk_hits() const { return tile_disk_hits_; }
    /// Tiles written to the persistent cache.
    [[nodiscard]] std::int64_t tile_disk_writes() const { return tile_disk_writes_; }

  private:
    // Outer ring as raw lon/lat positions (the only ring ever drawn, exactly
    // like polygon[0] in the Python renderer).
    using RawRing = std::vector<std::pair<double, double>>;  // (lon, lat)

    struct Ring {
        double minx, maxx, miny, maxy;
        std::vector<cv::Point> pts;  // rounded global pixels at this zoom
    };
    struct ZoomData {
        std::vector<Ring> countries;
        std::vector<Ring> admin2;
    };

    const ZoomData& zoom_data(int z) const;
    static std::vector<RawRing> parse_geojson(const std::string& path);

    std::vector<RawRing> countries_;
    std::vector<RawRing> admin2_;
    mutable std::unordered_map<int, ZoomData> zoom_cache_;
    // Per-render bounded cache: tiles are immutable and never exposed to callers.
    mutable std::map<std::tuple<int, std::int64_t, std::int64_t>, cv::Mat> tile_cache_;
    mutable std::int64_t tile_cache_hits_ = 0;
    mutable std::int64_t tile_cache_misses_ = 0;

    // Persistent cache. key = hash of the source rings, so a different
    // GeoJSON can never answer with another source's tile.
    DiskCacheOptions disk_;
    std::string disk_source_key_;
    mutable std::int64_t tile_disk_hits_ = 0;
    mutable std::int64_t tile_disk_writes_ = 0;
    mutable std::uint64_t disk_writes_since_sweep_ = 0;

    [[nodiscard]] std::string tile_disk_path(int z, std::int64_t x, std::int64_t y) const;
    [[nodiscard]] cv::Mat load_tile_from_disk(int z, std::int64_t x, std::int64_t y) const;
    void store_tile_to_disk(int z, std::int64_t x, std::int64_t y, const cv::Mat& tile) const;
    void enforce_disk_budget() const;
};

}  // namespace chrononplugin::maps
