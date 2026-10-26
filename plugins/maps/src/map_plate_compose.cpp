// Chronon3D native map-plate composition — TICKET-MAPS-NATIVE-PORT fase 2/4.
// Mirrors geo_runtime._ShotBuilder.pose ("dive") and
// dynamic_tile_pyramid.DynamicTilePyramid.sample_continuous /
// .render_plate_at_zoom exactly (see the header parity contract).

#include "chrononplugin/maps/map_plate_compose.hpp"

#include "chrononplugin/maps/map_camera.hpp"

#include <cmath>

#include <opencv2/imgproc.hpp>
#include <cstdint>

namespace chrononplugin::maps {

namespace {

/// Python round(v) — half-to-even — then int().
[[nodiscard]] int round_half_even(double v) {
    return static_cast<int>(std::nearbyint(v));
}

/// render_plate_at_zoom(center_lat, center_lon, zoom_int, width, height):
/// the plate window is centred on the anchor's global pixel at the integer
/// zoom and stitched from 256x256 tiles.
[[nodiscard]] cv::Mat render_plate_at_zoom(const VectorPlateRaster& raster,
                                           double lat, double lon, int zoom_int,
                                           int width, int height,
                                           PlateComposeStats* stats) {
    const auto [gx, gy] = latlon_to_global_px(lat, lon, zoom_int);
    const double left = gx - static_cast<double>(width) * 0.5;
    const double top = gy - static_cast<double>(height) * 0.5;
    if (stats != nullptr) {
        stats->plate_composes += 1;
        stats->plate_bytes += static_cast<std::int64_t>(width) * height * 3;
        // Every tile the window touches is rendered once per composition.
        const int tx0 = static_cast<int>(std::floor(left / 256.0));
        const int tx1 = static_cast<int>(std::ceil((left + width) / 256.0));
        const int ty0 = static_cast<int>(std::floor(top / 256.0));
        const int ty1 = static_cast<int>(std::ceil((top + height) / 256.0));
        stats->tile_renders += static_cast<std::int64_t>(tx1 - tx0) * (ty1 - ty0);
    }
    return raster.compose_plate(left, top, width, height, zoom_int);
}

}  // namespace

double builder_dive_zoom(double start_zoom, double end_zoom, int total_frames, int f) {
    // geo_runtime._ShotBuilder.pose, preset "dive".
    const int motion_frames = std::max(2, total_frames);
    const int clamped = std::min(f, motion_frames - 1);
    const int dive_end = round_half_even(static_cast<double>(motion_frames) * 0.62);
    const double e = smootherstep(static_cast<double>(clamped) /
                                  static_cast<double>(std::max(1, dive_end)));
    return start_zoom + (end_zoom - start_zoom) * e;
}

cv::Mat compose_sample_continuous(const VectorPlateRaster& raster,
                                  double lat, double lon, double continuous_zoom,
                                  int width, int height, PlateComposeStats* stats) {
    const int z_lo = static_cast<int>(std::floor(continuous_zoom));
    const int z_hi = z_lo + 1;
    const double t = continuous_zoom - static_cast<double>(z_lo);

    // Smoothstep easing on the blend parameter.
    const double alpha = t * t * (3.0 - 2.0 * t);

    const double scale_lo = std::pow(2.0, t);
    const double scale_hi = std::pow(2.0, t - 1.0);

    const int w_need_lo = round_half_even(static_cast<double>(width) / scale_lo) + 4;
    const int h_need_lo = round_half_even(static_cast<double>(height) / scale_lo) + 4;
    const cv::Mat crop_lo =
        render_plate_at_zoom(raster, lat, lon, z_lo, w_need_lo, h_need_lo, stats);
    cv::Mat plate_lo;
    cv::resize(crop_lo, plate_lo, {width, height}, 0.0, 0.0, cv::INTER_LINEAR);

    if (alpha <= 0.001) {
        return plate_lo;
    }
    const int w_need_hi = round_half_even(static_cast<double>(width) / scale_hi) + 4;
    const int h_need_hi = round_half_even(static_cast<double>(height) / scale_hi) + 4;
    const cv::Mat crop_hi =
        render_plate_at_zoom(raster, lat, lon, z_hi, w_need_hi, h_need_hi, stats);
    cv::Mat plate_hi;
    cv::resize(crop_hi, plate_hi, {width, height}, 0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat blended;
    cv::addWeighted(plate_lo, 1.0 - alpha, plate_hi, alpha, 0.0, blended);
    return blended;
}

}  // namespace chrononplugin::maps
