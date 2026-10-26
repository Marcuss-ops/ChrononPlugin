#pragma once

// Chronon3D native map camera/motion primitives — TICKET-MAPS-NATIVE-PORT
// fase 4 («flyover/camera sul contratto motion»).
//
// PARITY CONTRACT: every formula mirrors, in the same order of operations,
// the Python shot closures and projections the map renderer uses today:
//
//  * smootherstep          -> geo_runtime.py::_smootherstep
//  * roll_bank             -> render_geo_camera_small_places.py::roll_bank
//  * latlon_to_global_px   -> dynamic_tile_pyramid.py::latlon_to_global_px
//                             (Web Mercator, lat AND lon clamped)
//  * geo_to_screen         -> render_geo_camera_small_places.py::geo_to_screen
//  * pure_dive_motion      -> render_geo_camera_small_places.py::pure_dive_motion
//  * dive_tilt_motion      -> render_geo_camera_small_places.py::dive_tilt_motion
//
// "round" is Python's round(): half-to-even (std::nearbyint). The camera pose
// is a plain double tuple (lat, lon, zoom, pitch, yaw, roll) in exactly the
// order the Python closures return it, so a native pose is byte-comparable
// with the Python one.

#include <array>
#include <utility>

namespace chrononplugin::maps {

/// One sampled camera pose: (lat, lon, zoom, pitch, yaw, roll).
using CameraPose = std::array<double, 6>;

/// geo_runtime._smootherstep — the C2 law, clamped to [0, 1].
[[nodiscard]] double smootherstep(double p);

/// render_geo_camera_small_places.roll_bank.
[[nodiscard]] double roll_bank(double p);

/// dynamic_tile_pyramid.latlon_to_global_px — WGS84 to global Web Mercator
/// pixel coordinates (256-px tiles), lat clamped to ±85.05112878 and lon to
/// ±180.
[[nodiscard]] std::pair<double, double> latlon_to_global_px(double lat, double lon,
                                                            int zoom);

/// render_geo_camera_small_places.geo_to_screen — where a geographic point
/// lands on a nadir-plate frame whose anchor sits at the frame centre.
[[nodiscard]] std::pair<int, int> geo_to_screen(double lat, double lon,
                                                std::pair<double, double> anchor,
                                                double zoom, int width, int height);

/// pure_dive_motion(anchor, z0, z1, frames) — smootherstep zoom dive on the
/// anchor. `at(f)` for f in [0, frames-1].
[[nodiscard]] CameraPose pure_dive_pose(std::pair<double, double> anchor,
                                        double z0, double z1, int frames, int f);

/// dive_tilt_motion(anchor, z0, z1, dive_end, tilt_end, tilt_deg, frames) —
/// dive until `dive_end`, then tilt with roll bank until `tilt_end`.
[[nodiscard]] CameraPose dive_tilt_pose(std::pair<double, double> anchor,
                                        double z0, double z1, int dive_end,
                                        int tilt_end, double tilt_deg,
                                        int frames, int f);

}  // namespace chrononplugin::maps
