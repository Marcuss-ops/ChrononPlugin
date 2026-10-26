// Chronon3D native map camera/motion primitives — TICKET-MAPS-NATIVE-PORT
// fase 4. Pixel/pose parity with the Python closures (see the header): every
// expression below mirrors geo_runtime._smootherstep,
// render_geo_camera_small_places.{roll_bank,geo_to_screen,pure_dive_motion,
// dive_tilt_motion} and dynamic_tile_pyramid.latlon_to_global_px in the same
// order of operations. "round" is Python's round(): half-to-even.

#include "chrononplugin/maps/map_camera.hpp"

#include <algorithm>
#include <cmath>

namespace chrononplugin::maps {

namespace {

/// math.radians(x) == x * (pi / 180.0) in CPython.
[[nodiscard]] double radians(double deg) { return deg * (M_PI / 180.0); }

/// Python round(v) — half-to-even — then int().
[[nodiscard]] int round_half_even(double v) {
    return static_cast<int>(std::nearbyint(v));
}

[[nodiscard]] double clamp01(double p) { return std::min(std::max(p, 0.0), 1.0); }

}  // namespace

double smootherstep(double p) {
    const double t = clamp01(p);
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double roll_bank(double p) {
    return -8.0 * std::sin(clamp01(p) * M_PI);
}

std::pair<double, double> latlon_to_global_px(double lat, double lon, int zoom) {
    // dynamic_tile_pyramid.latlon_to_global_px: both coordinates clamped.
    lat = std::min(std::max(lat, -85.05112878), 85.05112878);
    lon = std::min(std::max(lon, -180.0), 180.0);
    const double scale = 256.0 * static_cast<double>(std::int64_t{1} << zoom);
    const double x = (lon + 180.0) / 360.0 * scale;
    const double lat_rad = radians(lat);
    const double y =
        (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) * 0.5 * scale;
    return {x, y};
}

std::pair<int, int> geo_to_screen(double lat, double lon,
                                  std::pair<double, double> anchor,
                                  double zoom, int width, int height) {
    const double zf = std::floor(zoom);
    const double scale = std::pow(2.0, zoom - zf);
    const auto [gx, gy] = latlon_to_global_px(lat, lon, static_cast<int>(zf));
    const auto [ax, ay] = latlon_to_global_px(anchor.first, anchor.second,
                                              static_cast<int>(zf));
    return {round_half_even(static_cast<double>(width) / 2.0 + (gx - ax) * scale),
            round_half_even(static_cast<double>(height) / 2.0 + (gy - ay) * scale)};
}

CameraPose pure_dive_pose(std::pair<double, double> anchor,
                          double z0, double z1, int frames, int f) {
    // pure_dive_motion.at(f)
    const double e = smootherstep(static_cast<double>(f) /
                                  static_cast<double>(std::max(1, frames - 1)));
    return {anchor.first, anchor.second, z0 + (z1 - z0) * e, 0.0, 0.0, 0.0};
}

CameraPose dive_tilt_pose(std::pair<double, double> anchor,
                          double z0, double z1, int dive_end,
                          int tilt_end, double tilt_deg, int frames, int f) {
    // dive_tilt_motion.at(f)
    if (f <= dive_end) {
        const double e = smootherstep(static_cast<double>(f) /
                                      static_cast<double>(std::max(1, dive_end)));
        return {anchor.first, anchor.second, z0 + (z1 - z0) * e, 0.0, 0.0, 0.0};
    }
    const double p = static_cast<double>(f - dive_end) /
                     static_cast<double>(std::max(1, tilt_end - dive_end));
    const double e = smootherstep(p);
    return {anchor.first, anchor.second, z1, tilt_deg * e, 0.0, roll_bank(p)};
}

}  // namespace chrononplugin::maps
