#pragma once

// Private to plugins/maps: the projection, rounding and palette helpers shared
// by the two halves of the vector plate raster (tile rendering in
// map_plate_raster_tiles.cpp, lifecycle and persistent cache in
// map_plate_raster.cpp). Every formula mirrors vector_plate_pyramid.py in the
// same order of operations; the parity contract lives in the public header.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include <opencv2/core.hpp>

namespace chrononplugin::maps::raster_detail {

inline constexpr int kTile = 256;
inline constexpr double kWebMercatorLatLimit = 85.05112878;

// BGR paper palette (values identical to vector_plate_pyramid.py).
inline const cv::Scalar kSea(245, 247, 247);
inline const cv::Scalar kLand(224, 226, 226);
inline const cv::Scalar kBorder(188, 190, 190);
inline const cv::Scalar kGrid(234, 236, 236);
inline const cv::Scalar kItalyAdminLine(170, 172, 172);

// (lon_min, lon_max, lat_min, lat_max) — ITALY_BBOX.
inline constexpr double kItalyLonMin = 6.5;
inline constexpr double kItalyLonMax = 18.5;
inline constexpr double kItalyLatMin = 35.0;
inline constexpr double kItalyLatMax = 47.5;

/// Python's modulo for tile x wrapping (result in [0, m)).
[[nodiscard]] inline std::int64_t py_mod(std::int64_t x, std::int64_t m) {
    const std::int64_t r = x % m;
    return r < 0 ? r + m : r;
}

[[nodiscard]] inline double clamp_lat(double lat) {
    return std::min(std::max(lat, -kWebMercatorLatLimit), kWebMercatorLatLimit);
}

/// math.radians(x) == x * (pi / 180.0) in CPython.
[[nodiscard]] inline double radians(double deg) { return deg * (M_PI / 180.0); }
/// math.degrees(x) == x * (180.0 / pi) in CPython.
[[nodiscard]] inline double degrees(double rad) { return rad * (180.0 / M_PI); }

/// Python round(v) — half-to-even — then int().
[[nodiscard]] inline int round_half_even(double v) {
    return static_cast<int>(std::nearbyint(v));
}

/// _global_px(lat, lon, zoom): global pixel coords at 256-px tiles.
[[nodiscard]] inline std::pair<double, double> global_px(double lat, double lon,
                                                         int zoom) {
    const double scale = 256.0 * static_cast<double>(std::int64_t{1} << zoom);
    const double x = (lon + 180.0) / 360.0 * scale;
    const double lat_rad = radians(clamp_lat(lat));
    const double y =
        (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) * 0.5 * scale;
    return {x, y};
}

/// _tile_latlon(z, x, y): (lon_min, lon_max, lat_min, lat_max).
struct TileBBox {
    double lon_min, lon_max, lat_min, lat_max;
};

[[nodiscard]] inline TileBBox tile_latlon(int z, std::int64_t x, std::int64_t y) {
    const double scale = 256.0 * static_cast<double>(std::int64_t{1} << z);
    TileBBox b{};
    b.lon_min = (static_cast<double>(x) * 256.0 / scale) * 360.0 - 180.0;
    b.lon_max = (static_cast<double>(x + 1) * 256.0 / scale) * 360.0 - 180.0;
    const double n_top = M_PI - 2.0 * M_PI * (static_cast<double>(y) * 256.0 / scale);
    const double n_bot = M_PI - 2.0 * M_PI * (static_cast<double>(y + 1) * 256.0 / scale);
    b.lat_max = degrees(std::atan(std::sinh(n_top)));
    b.lat_min = degrees(std::atan(std::sinh(n_bot)));
    return b;
}

/// _graticule_step_deg(zoom).
[[nodiscard]] inline double graticule_step_deg(int zoom) {
    if (zoom <= 6) return 10.0;
    if (zoom <= 8) return 5.0;
    if (zoom <= 10) return 2.0;
    return 1.0;
}

}  // namespace chrononplugin::maps::raster_detail
