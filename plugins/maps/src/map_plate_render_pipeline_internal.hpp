#pragma once

// Private contract shared by the native map-plate pipeline, the per-frame
// compositor (map_plate_render_frames.cpp) and the certified telemetry writer
// (map_plate_render_telemetry.cpp). Not installed: only the split translation
// units of the renderer include it.

#include "chrononplugin/maps/map_plate_render.hpp"

#include "chrononplugin/maps/esri_satellite.hpp"
#include "chrononplugin/maps/geo_feature.hpp"
#include "chrononplugin/maps/map_plate_compose.hpp"
#include "chrononplugin/maps/map_plate_raster.hpp"
#include "chrononplugin/maps/map_plate_telemetry.hpp"
#include "chrononplugin/maps/map_story_effects.hpp"
#include "map_plate_render_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

namespace chrononplugin::maps::map_plate_detail {

/// Everything a single frame needs from the validated payload: the prepared
/// sources, the geographic anchor, the copy and the timing contract. Pointers
/// are null when the treatment does not use that source.
struct MapPlateScene {
    const VectorPlateRaster* raster = nullptr;
    EsriSatellitePyramid* satellite = nullptr;
    std::pair<double, double> anchor{0.0, 0.0};
    bool self_contained_map = false;
    std::string camera_animation;
    std::string map_animation;
    std::string map_place;
    bool show_text = true;
    std::vector<Pin> pins;
    std::vector<GeoPolygon> named_place_polygons;
    std::vector<GeoPolygon> territory_polygons;
    GeoFeatureCollection country_features;
    cv::Mat mountain_overlay;
    const nlohmann::json* payload = nullptr;
    int output_width = 0;
    int output_height = 0;
    std::int64_t frames = 0;
    double previous_zoom = 0.0;
    int fps_num = 30;
    int fps_den = 1;
    std::int64_t duration_us = 0;
    std::int64_t motion_duration_us = 0;
    std::vector<double> route_altitudes;
    double vignette_strength = 0.0;
    double grain_strength = 0.0;
    bool profiling = false;
    TacticalPhaseTimings* tactical_timings = nullptr;
};

/// Per-frame wall time split by phase; the driver accumulates it.
struct FrameTimings {
    double compose_s = 0.0;
    double effect_s = 0.0;
    double finish_s = 0.0;
};

/// Composes the base plate, applies the map animation, the named-place fill,
/// the imagery credits and the documentary finish. `zoom` comes from the
/// deterministic camera schedule.
///
/// `cached_plate` skips the base-plate composition when the camera pose did not
/// change from the previous frame (a settled camera holds for most of a clip);
/// `plate_out` hands the pristine pre-effect plate back to the driver so the
/// next frame at the same pose can reuse it. The plate is a pure function of
/// (anchor, zoom, size), so reuse is pixel-identical; the animation, fill and
/// finish are still applied per frame.
[[nodiscard]] cv::Mat render_plate_frame(const MapPlateScene& scene,
                                         std::int64_t frame_index, double zoom,
                                         PlateComposeStats& stats,
                                         FrameTimings& timings,
                                         const cv::Mat* cached_plate = nullptr,
                                         cv::Mat* plate_out = nullptr);

/// Measured counters the certified summary is built from.
struct TelemetryInputs {
    std::int64_t frames = 0;
    int width = 0;
    int height = 0;
    int fps_num = 30;
    int fps_den = 1;
    std::string rendered_basemap;
    const EsriSatellitePyramid* satellite = nullptr;
    const VectorPlateRaster* raster = nullptr;
    bool self_contained_map = false;
    double prepare_ms = 0.0;
    double effect_wall_s = 0.0;
    double compose_wall_s = 0.0;
    double frame_wall_s = 0.0;
    double pipeline_wall_s = 0.0;
    double total_wall_s = 0.0;
    PlateComposeStats stats;
    std::size_t frame_bytes = 0;
    std::int64_t output_bytes = 0;
    EncoderProfile profile;
    std::string sampler;
};

[[nodiscard]] DynamicMapTelemetry build_map_plate_telemetry(
    const TelemetryInputs& inputs);

/// Writes the staged summary and renames video + summary into place. Returns
/// the failure message, or nullptr once both artifacts are installed.
[[nodiscard]] const char* install_plate_outputs(
    const MapPlateRequest& request, const std::string& summary_json,
    const std::string& staged_video, const std::string& staged_summary);

}  // namespace chrononplugin::maps::map_plate_detail
