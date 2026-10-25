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

class VectorPlateRaster {
  public:
    explicit VectorPlateRaster(VectorMapSources sources);

    /// One 256x256 BGR paper tile, mirroring render_vector_tile(z, x, y).
    /// x is wrapped like Python's modulo, y outside the pyramid yields the
    /// blank sea tile — the same contract as VectorPlatePyramid.
    [[nodiscard]] cv::Mat render_tile(int z, std::int64_t x, std::int64_t y) const;

    /// Stitch tiles into a plate window, mirroring FastPlateSampler._compose_px.
    [[nodiscard]] cv::Mat compose_plate(double left, double top,
                                        int width, int height, int zoom) const;

    /// Number of outer rings carried at a zoom (diagnostics only).
    [[nodiscard]] std::size_t ring_count(int z) const;

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
};

}  // namespace chrononplugin::maps
