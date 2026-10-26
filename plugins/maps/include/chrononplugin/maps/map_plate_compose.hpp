#pragma once

// Chronon3D native map-plate composition — TICKET-MAPS-NATIVE-PORT fase 2/4.
//
// PARITY CONTRACT (bit-exact, verified by
// tools/map_native/parity_composite_check.py): the frame composition mirrors
// the product sampling glue in the same order of operations:
//
//  * builder_dive_zoom       -> geo_runtime._ShotBuilder.pose, "dive" preset
//                               (dive_end = round(motion_frames * 0.62),
//                               smootherstep over dive_end)
//  * compose_sample_continuous -> dynamic_tile_pyramid.DynamicTilePyramid
//                               .sample_continuous + .render_plate_at_zoom
//                               (smoothstep cross-fade between z_lo and z_hi,
//                               INTER_LINEAR resizes, +4 px sampling margins)
//
// "round" is Python's round(): half-to-even. Nothing here invents geometry:
// the raster is a pure function of the sources plus (lat, lon, zoom, size).

#include <cstdint>
#include <string>

#include <opencv2/core.hpp>

#include "chrononplugin/maps/map_plate_raster.hpp"

namespace chrononplugin::maps {

/// The "dive" motion of the product shot builder: smootherstep zoom from
/// start_zoom to end_zoom, completing at round(motion_frames * 0.62) like
/// geo_runtime._ShotBuilder.pose. `f` is clamped to motion_frames-1.
[[nodiscard]] double builder_dive_zoom(double start_zoom, double end_zoom,
                                       int total_frames, int f);

/// Counts the tiles a composition renders (telemetry accounting).
struct PlateComposeStats {
    std::int64_t tile_renders = 0;    // render_tile calls
    std::int64_t plate_composes = 0;  // composed plates (lo + hi)
    std::int64_t plate_bytes = 0;     // bytes of composed plates
};

/// sample_continuous(center_lat, center_lon, continuous_zoom, width, height):
/// composes the lo plate at floor(zoom) and — when the blend alpha exceeds
/// 0.001 — the hi plate at floor(zoom)+1, resizes both INTER_LINEAR to
/// (width, height) and cross-fades them with the smoothstep alpha.
[[nodiscard]] cv::Mat compose_sample_continuous(const VectorPlateRaster& raster,
                                                double lat, double lon,
                                                double continuous_zoom,
                                                int width, int height,
                                                PlateComposeStats* stats = nullptr);

}  // namespace chrononplugin::maps
