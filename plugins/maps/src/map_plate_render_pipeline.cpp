// Chronon3D native map-plate rendering — TICKET-MAPS-NATIVE-PORT.
//
// Native flyover renderer: orchestration only. Validation, source preparation,
// the encoder process, frame scheduling and the certified summary hand-off;
// the per-frame compositor lives in map_plate_render_frames.cpp, the telemetry
// and output installation in map_plate_render_telemetry.cpp, and the closed
// encoder contract and payload validation in map_plate_render_encoder.cpp and
// map_plate_render_payload.cpp. Outputs are staged and installed only together
// with a complete telemetry summary; every payload outside the native subset is
// refused, never guessed.

#include "chrononplugin/maps/map_plate_render.hpp"

#include "map_plate_render_internal.hpp"
#include "map_plate_render_pipeline_internal.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

#include "chrononplugin/maps/esri_satellite.hpp"
#include "chrononplugin/maps/geojson.hpp"
#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_plate_compose.hpp"
#include "chrononplugin/maps/map_plate_raster.hpp"
#include "chrononplugin/maps/map_plate_telemetry.hpp"
#include "chrononplugin/maps/map_story_effects.hpp"
#include <cstdint>
#include <cstddef>

namespace chrononplugin::maps {
using namespace map_plate_detail;

MapPlateRenderResult render_map_plate(const MapPlateRequest& request) {
    if (request.plate_id.empty() || request.payload_json.empty() ||
        request.output_width <= 0 || request.output_height <= 0 ||
        request.frame_rate <= 0.0 || request.frame_count <= 0 ||
        request.output_video.empty() || request.summary_output.empty()) {
        return {MapPlateStatus::kInvalidRequest,
                "map-plate request fails contract validation (plate id, payload, "
                "output size, frame rate, frame count, output video and summary "
                "output are all required)"};
    }

    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(request.payload_json);
    } catch (const std::exception& exc) {
        return {MapPlateStatus::kInvalidRequest,
                std::string("map-plate payload is not valid JSON: ") + exc.what()};
    }
    if (!payload.is_object()) {
        return {MapPlateStatus::kInvalidRequest, "map-plate payload must be an object"};
    }

    std::string error;
    if (!known_payload_keys(payload, error)) {
        return {MapPlateStatus::kNotImplemented, error};
    }
    if (!known_pin_keys(payload, error)) {
        return {MapPlateStatus::kNotImplemented, error};
    }
    if (!supported_semantics(payload, error)) {
        return {MapPlateStatus::kNotImplemented, error};
    }

    // Media contract: the payload and the request must agree.
    if (payload.contains("width") && payload.at("width").get<int>() != request.output_width) {
        return {MapPlateStatus::kInvalidRequest,
                "payload width does not match the request output width"};
    }
    if (payload.contains("height") &&
        payload.at("height").get<int>() != request.output_height) {
        return {MapPlateStatus::kInvalidRequest,
                "payload height does not match the request output height"};
    }
    // The media contract's exact rational frame rate (the cache identity
    // compares num/den, so it is preserved verbatim).
    int fps_num = static_cast<int>(std::lround(request.frame_rate * 1000.0));
    int fps_den = 1000;
    if (payload.contains("fps_num") || payload.contains("fps_den")) {
        const int num = payload.value("fps_num", 0);
        const int den = payload.value("fps_den", 0);
        if (num <= 0 || den <= 0 ||
            std::abs(request.frame_rate -
                     static_cast<double>(num) / static_cast<double>(den)) > 1e-9) {
            return {MapPlateStatus::kInvalidRequest,
                    "payload frame rate does not match the request frame rate"};
        }
        fps_num = num;
        fps_den = den;
    }

    // Camera/label animations.
    const std::string camera_animation = payload.value("camera_animation", "slow_approach");
    const std::string label_animation = payload.value("label_animation", "none");
    if (supported_map_label_animations().count(label_animation) == 0) {
        return {MapPlateStatus::kNotImplemented,
                "native map subset supports label_animation 'none' only, got '" +
                    label_animation + "'"};
    }

    // Encoder profile and sampler (closed contract).
    EncoderProfile profile{"libx264", false, "veryfast", 23};
    if (payload.contains("encoder_profile")) {
        if (!resolve_encoder_profile(payload.at("encoder_profile"), profile, error)) {
            return {MapPlateStatus::kInvalidRequest, error};
        }
    }
    std::string sampler = payload.value("sampler", "");
    if (sampler.empty()) {
        // render_dynamic_map_overlay.default_map_sampler
        sampler = "cpu";
    }
    if (sampler != "opencv_opencl" && sampler != "cpu") {
        return {MapPlateStatus::kInvalidRequest,
                "unsupported declared map sampler: " + sampler};
    }
    if (sampler != "cpu") {
        return {MapPlateStatus::kNotImplemented,
                "the native vector plates are CPU-sampled; sampler '" + sampler +
                    "' is not available natively yet"};
    }
    // Pins and camera anchor.
    const std::string map_animation = payload.value("map_animation", "");
    // These compositions paint their own screen-space plate. Building a
    // basemap pyramid for them would be work whose pixels are overwritten.
    const bool self_contained_map =
        map_animation == "territory_tactical_fill" ||
        map_animation == "volumetric_laser_scan" ||
        map_animation == "vector_particle_flow" ||
        map_animation == "dark_mirror_diorama" ||
        map_animation == "voronoi_heat_contours" ||
        map_animation == "holographic_postfx";
    const std::string base = catalog_dir();
    if (base.empty()) {
        return {MapPlateStatus::kRenderFailed,
                "no map boundary catalog configured (CHRONON_MAPS_CATALOG_DIR)"};
    }
    const std::string countries = base + "/ne_50m_admin_0_countries.geojson";
    const std::string admin2 =
        base + "/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson";
    const std::string map_place = payload.value("map_place", "");
    PlaceViewport place_viewport;
    std::vector<GeoPolygon> named_place_polygons;
    if (!map_place.empty() &&
        !load_named_place(base, map_place, named_place_polygons,
                          place_viewport, request.output_width,
                          request.output_height, error)) {
        return {MapPlateStatus::kInvalidRequest, error};
    }
    const bool show_text = payload.value("show_text", true);
    std::vector<Pin> pins;
    if (!normalize_pins(payload.value("pins", nlohmann::json::array()), pins,
                         error, !map_place.empty())) {
        return {MapPlateStatus::kInvalidRequest, error};
    }
    if (pins.empty() && !map_place.empty()) {
        pins.push_back(Pin{"", place_viewport.anchor.first,
                           place_viewport.anchor.second});
    }
    if (!show_text) {
        for (auto& pin : pins) pin.text.clear();
    }
    if ((map_animation == "route_line" || map_animation == "multi_leg_flight") &&
        pins.size() < 2 && map_place.empty()) {
        return {MapPlateStatus::kInvalidRequest,
                "route animation requires at least two grounded map pins"};
    }
    std::vector<double> route_altitudes;
    if (payload.contains("route_altitudes_m")) {
        const auto& values = payload.at("route_altitudes_m");
        if (!values.is_array() || values.size() != pins.size()) {
            return {MapPlateStatus::kInvalidRequest,
                    "route_altitudes_m must contain one altitude per pin"};
        }
        for (const auto& value : values) {
            if (!value.is_number() || !std::isfinite(value.get<double>()) ||
                value.get<double>() < 0.0) {
                return {MapPlateStatus::kInvalidRequest,
                        "route altitudes must be finite non-negative metres"};
            }
            route_altitudes.push_back(value.get<double>());
        }
    }
    const auto finish = payload.value("documentary_finish", nlohmann::json::object());
    if (!finish.is_object()) {
        return {MapPlateStatus::kInvalidRequest, "documentary_finish must be an object"};
    }
    for (auto it = finish.begin(); it != finish.end(); ++it) {
        if (it.key() != "vignette" && it.key() != "grain") {
            return {MapPlateStatus::kNotImplemented,
                    "unsupported documentary_finish field: " + it.key()};
        }
    }
    const double vignette_strength = finish.value("vignette", 0.0);
    const double grain_strength = finish.value("grain", 0.0);
    if (!std::isfinite(vignette_strength) || vignette_strength < 0.0 || vignette_strength > 0.8 ||
        !std::isfinite(grain_strength) || grain_strength < 0.0 || grain_strength > 0.12) {
        return {MapPlateStatus::kInvalidRequest,
                "documentary_finish vignette/grain values are outside their supported range"};
    }
    std::pair<double, double> anchor = map_place.empty()
        ? std::pair<double, double>{pins.front().lat, pins.front().lon}
        : place_viewport.anchor;
    if (payload.contains("camera_anchor")) {
        const auto& a = payload.at("camera_anchor");
        if (!a.is_array() || a.size() != 2) {
            return {MapPlateStatus::kInvalidRequest,
                    "camera_anchor must be a [lat, lon] pair"};
        }
        anchor = {a.at(0).get<double>(), a.at(1).get<double>()};
    } else if (pins.size() > 1 && map_place.empty()) {
        return {MapPlateStatus::kNotImplemented,
                "multi-pin map tours are not ported natively yet; provide an "
                "explicit camera_anchor or a single pin"};
    }
    const bool auto_place_zoom = !map_place.empty() &&
        !payload.contains("camera_start_zoom") && !payload.contains("camera_end_zoom");
    const double default_start_zoom = auto_place_zoom
        ? std::max(0.0, place_viewport.fit_zoom - 0.24) : 5.2;
    const double default_end_zoom = auto_place_zoom
        ? std::min(18.0, place_viewport.fit_zoom + 0.08) : 17.8;
    const double start_zoom = payload.value("camera_start_zoom", default_start_zoom);
    const double end_zoom = payload.value("camera_end_zoom", default_end_zoom);
    const std::int64_t duration_us = payload.value("duration_us", std::int64_t{0});
    const std::int64_t motion_duration_us = payload.value(
        "camera_motion_duration_us", duration_us);
    if (motion_duration_us <= 0 || motion_duration_us > duration_us) {
        return {MapPlateStatus::kInvalidRequest,
                "camera_motion_duration_us must be positive and no longer than duration_us"};
    }
    const int frame_count = static_cast<int>(request.frame_count);
    std::vector<double> frame_zooms;
    frame_zooms.reserve(static_cast<std::size_t>(frame_count));
    const int motion_frame_count = std::max(2, static_cast<int>(std::ceil(
        static_cast<double>(motion_duration_us) * fps_num /
        (1e6 * static_cast<double>(fps_den)))));
    for (int f = 0; f < frame_count; ++f) {
        if (camera_animation == "exponential_dive") {
            const double p = std::clamp(static_cast<double>(f) /
                static_cast<double>(std::max(1, motion_frame_count - 1)), 0.0, 1.0);
            const double eased = (1.0 - std::pow(2.0, -10.0 * p)) /
                                 (1.0 - std::pow(2.0, -10.0));
            frame_zooms.push_back(start_zoom + (end_zoom - start_zoom) * eased);
        } else {
            const int camera_frame = std::min(f, motion_frame_count - 1);
            frame_zooms.push_back(builder_dive_zoom(start_zoom, end_zoom,
                                                    motion_frame_count, camera_frame));
        }
    }

    std::vector<GeoPolygon> territory_polygons;
    if (!map_place.empty()) territory_polygons = named_place_polygons;
    GeoFeatureCollection country_features;
    cv::Mat mountain_overlay;

    const auto total_start = std::chrono::steady_clock::now();
    const auto prepare_start = total_start;
    std::unique_ptr<VectorPlateRaster> raster;
    std::unique_ptr<EsriSatellitePyramid> satellite;
    try {
        if (map_animation == "mountain_ridge_callout" &&
            payload.contains("mountain_overlay_asset")) {
            std::string overlay_path =
                payload.at("mountain_overlay_asset").get<std::string>();
            mountain_overlay = cv::imread(overlay_path, cv::IMREAD_UNCHANGED);
            if (mountain_overlay.empty() &&
                !std::filesystem::path(overlay_path).is_absolute()) {
                overlay_path = base + "/" + overlay_path;
                mountain_overlay = cv::imread(overlay_path, cv::IMREAD_UNCHANGED);
            }
            if (mountain_overlay.empty() || mountain_overlay.channels() != 4) {
                throw std::runtime_error(
                    "mountain_overlay_asset must be a readable RGBA image: " +
                    overlay_path);
            }
        }
        const bool style_morph = map_animation == "map_style_morph";
        const std::string basemap = payload.value("basemap", "chronon_editorial");
        const bool esri_tile_basemap = basemap == "esri_sat" || basemap == "esri_topo";
        // A self-contained treatment never samples the base plate; do not
        // construct the vector or satellite pyramid at all.
        if (!self_contained_map && (esri_tile_basemap || style_morph)) {
            satellite = std::make_unique<EsriSatellitePyramid>(
                "", basemap == "esri_topo" ? "esri_topo" : "esri_sat");
            satellite->prefetch_trajectory(anchor.first, anchor.second,
                frame_zooms,
                request.output_width, request.output_height);
        }		if (!self_contained_map && (!esri_tile_basemap || style_morph)) {
			// ELIM-05: opt-in persistent tile cache. Empty/unset env keeps the
			// historical per-render behaviour; the directory is bounded and the
			// tiles are content-addressed by source geometry.
			chrononplugin::maps::DiskCacheOptions disk;
			if (const char* cache_dir = std::getenv("CHRONON_MAP_TILE_CACHE_DIR");
				cache_dir != nullptr && *cache_dir != '\0') {
				disk.dir = cache_dir;
			}
			raster = std::make_unique<VectorPlateRaster>(VectorMapSources{countries, admin2}, disk);
		}
        if (map_place.empty() && (map_animation == "territory_glow_reveal" ||
            map_animation == "satellite_scan_reveal" ||
            map_animation == "territory_flood_fill" ||
            map_animation == "territory_tactical_fill" ||
            map_animation == "territory_extrusion" ||
            map_animation == "volumetric_laser_scan" ||
            map_animation == "vector_particle_flow" ||
            map_animation == "dark_mirror_diorama" ||
            map_animation == "voronoi_heat_contours" ||
            map_animation == "holographic_postfx")) {
            std::string path = base + "/italy_admin/geoBoundaries_ITA_ADM3.simplified.geojson";
            if (payload.contains("territory_geojson")) {
                path = payload.at("territory_geojson").get<std::string>();
            }
            std::ifstream input(path);
            if (!input && !path.empty() && path.front() != '/') {
                input.clear();
                input.open(base + "/" + path);
            }
            if (!input) throw std::runtime_error("cannot open territory GeoJSON: " + path);
            const std::string json((std::istreambuf_iterator<char>(input)), {});
            const auto collection = parse_geojson(json);
            const std::string target_name = payload.at("territory_feature").get<std::string>();
            for (const auto& feature : collection.features) {
                const auto found = feature.properties.find("shapeName");
                if (found != feature.properties.end() && found->second == target_name) {
                    territory_polygons = feature.polygons;
                    break;
                }
            }
            if (territory_polygons.empty())
                throw std::runtime_error("territory feature not found in GeoJSON: " + target_name);
        }
        if (map_animation == "continental_atlas" ||
            (map_animation == "historic_location_timeline" && map_place.empty())) {
            std::ifstream input(countries);
            if (!input) throw std::runtime_error("cannot open country GeoJSON: " + countries);
            const std::string json((std::istreambuf_iterator<char>(input)), {});
            const auto collection = parse_geojson(json);
            std::string target_name = payload.value("country_feature", map_place);
            if (map_animation == "continental_atlas") {
                country_features = collection;
            } else {
                for (const auto& feature : collection.features) {
                    const auto found = feature.properties.find("ADMIN");
                    if (found != feature.properties.end() && found->second == target_name) {
                        territory_polygons = feature.polygons;
                        break;
                    }
                }
            }
            if (map_animation == "historic_location_timeline" && territory_polygons.empty())
                throw std::runtime_error("country feature not found in GeoJSON: " + target_name);
        }
    } catch (const std::exception& exc) {
        return {MapPlateStatus::kRenderFailed,
                std::string("cannot prepare native map sources: ") + exc.what()};
    }
    const double prepare_ms = seconds_since(prepare_start) * 1e3;

    // Stage the video and the summary; install both only when certified.
    // Staged names keep the artifact suffixes so ffmpeg infers the container.
    const std::string staged_video = request.output_video + ".staged.mp4";
    const std::string staged_summary = request.summary_output + ".staged.json";

    const std::string ffmpeg_cmd = map_plate_ffmpeg_command(
        request, fps_num, fps_den, profile, staged_video);

    std::FILE* encoder = ::popen(ffmpeg_cmd.c_str(), "w");
    if (encoder == nullptr) {
        return {MapPlateStatus::kRenderFailed,
                "cannot launch the ffmpeg encoder for the map plate"};
    }

    PlateComposeStats stats;
    double compose_wall = 0.0;
    double effect_wall = 0.0;
    double frame_wall = 0.0;
    double pipe_write_wall = 0.0;
    double finish_wall = 0.0;
    TacticalPhaseTimings tactical_timings;
    const bool profiling = std::getenv("CHRONON_MAP_PROFILE") != nullptr;
    bool io_error = false;
    const std::size_t frame_bytes = static_cast<std::size_t>(request.output_width) *
                                    request.output_height * 3;
    std::vector<unsigned char> buffer(frame_bytes);

    MapPlateScene scene;
    scene.raster = raster.get();
    scene.satellite = satellite.get();
    scene.anchor = anchor;
    scene.self_contained_map = self_contained_map;
    scene.camera_animation = camera_animation;
    scene.map_animation = map_animation;
    scene.map_place = map_place;
    scene.show_text = show_text;
    scene.pins = pins;
    scene.named_place_polygons = named_place_polygons;
    scene.territory_polygons = territory_polygons;
    scene.country_features = country_features;
    scene.mountain_overlay = mountain_overlay;
    scene.payload = &payload;
    scene.output_width = request.output_width;
    scene.output_height = request.output_height;
    scene.frames = request.frame_count;
    scene.fps_num = fps_num;
    scene.fps_den = fps_den;
    scene.duration_us = duration_us;
    scene.motion_duration_us = motion_duration_us;
    scene.route_altitudes = route_altitudes;
    scene.vignette_strength = vignette_strength;
    scene.grain_strength = grain_strength;
    scene.profiling = profiling;
    scene.tactical_timings = &tactical_timings;

    const auto pipeline_start = std::chrono::steady_clock::now();
    // A settled camera keeps the same pose for the rest of the clip, so the base
    // plate is recomposed once and then reused; only the per-frame treatment
    // runs. The pose comparison is exact because both sides come from the same
    // deterministic schedule.
    cv::Mat cached_plate;
    double cached_plate_zoom = 0.0;
    for (std::int64_t f = 0; f < request.frame_count && !io_error; ++f) {
        const auto frame_start = std::chrono::steady_clock::now();
        const double zoom = frame_zooms[static_cast<std::size_t>(f)];
        scene.previous_zoom = f > 0
            ? frame_zooms[static_cast<std::size_t>(f - 1)]
            : 0.0;
        FrameTimings timings;
        const bool reuse_plate = f > 0 && !cached_plate.empty() &&
                                 zoom == cached_plate_zoom;
        cv::Mat frame = render_plate_frame(
            scene, f, zoom, stats, timings,
            reuse_plate ? &cached_plate : nullptr,
            reuse_plate ? nullptr : &cached_plate);
        if (!reuse_plate) {
            cached_plate_zoom = cached_plate.empty() ? 0.0 : zoom;
        }
        compose_wall += timings.compose_s;
        if (self_contained_map) {
            effect_wall += timings.effect_s;
        }
        finish_wall += timings.finish_s;
        if (!request.raw_frames_dir.empty()) {
            const std::string path = request.raw_frames_dir + "/frame_" +
                                     std::to_string(1000000 + f).substr(1) + ".raw";
            std::FILE* raw = std::fopen(path.c_str(), "wb");
            if (raw == nullptr ||
                std::fwrite(frame.data, 1, frame_bytes, raw) != frame_bytes) {
                io_error = true;
            }
            if (raw != nullptr) std::fclose(raw);
        }
        const auto pipe_start = std::chrono::steady_clock::now();
        if (std::fwrite(frame.data, 1, frame_bytes, encoder) != frame_bytes) {
            io_error = true;
        }
        pipe_write_wall += seconds_since(pipe_start);
        frame_wall += seconds_since(frame_start);
    }
    const double pipeline_wall = seconds_since(pipeline_start);

    const auto drain_start = std::chrono::steady_clock::now();
    const int encoder_status = ::pclose(encoder);
    const double encoder_drain_wall = seconds_since(drain_start);
    if (profiling) {
        const double frames = static_cast<double>(
            std::max<std::int64_t>(1, request.frame_count));
        const auto per_frame = [frames](double ms) { return ms / frames; };
        std::fprintf(stderr,
            "[map-profile] frames=%lld effect_ms=%.2f grid_ms=%.2f mask_ms=%.2f "
            "fill_ms=%.2f contours_ms=%.2f glow_draw_ms=%.2f blur_ms=%.2f "
            "outline_ms=%.2f label_ms=%.2f pipe_write_ms=%.2f finish_ms=%.2f "
            "encoder_drain_ms=%.2f total_ms=%.2f\n",
            static_cast<long long>(request.frame_count),
            per_frame(effect_wall * 1e3), per_frame(tactical_timings.grid_ms),
            per_frame(tactical_timings.mask_ms), per_frame(tactical_timings.fill_ms),
            per_frame(tactical_timings.contours_ms),
            per_frame(tactical_timings.glow_draw_ms),
            per_frame(tactical_timings.blur_ms),
            per_frame(tactical_timings.outline_ms),
            per_frame(tactical_timings.label_ms), per_frame(pipe_write_wall * 1e3),
            per_frame(finish_wall * 1e3), encoder_drain_wall * 1e3,
            seconds_since(total_start) * 1e3);
    }
    if (io_error || encoder_status != 0) {
        std::remove(staged_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "the native map render pipeline failed while writing frames"};
    }
    std::FILE* staged = std::fopen(staged_video.c_str(), "rb");
    std::int64_t output_bytes = 0;
    if (staged != nullptr) {
        std::fseek(staged, 0, SEEK_END);
        output_bytes = static_cast<std::int64_t>(std::ftell(staged));
        std::fclose(staged);
    }

    TelemetryInputs telemetry_inputs;
    telemetry_inputs.frames = request.frame_count;
    telemetry_inputs.width = request.output_width;
    telemetry_inputs.height = request.output_height;
    telemetry_inputs.fps_num = fps_num;
    telemetry_inputs.fps_den = fps_den;
    telemetry_inputs.rendered_basemap =
        payload.value("basemap", "chronon_editorial");
    telemetry_inputs.satellite = satellite.get();
    telemetry_inputs.raster = raster.get();
    telemetry_inputs.self_contained_map = self_contained_map;
    telemetry_inputs.prepare_ms = prepare_ms;
    telemetry_inputs.effect_wall_s = effect_wall;
    telemetry_inputs.compose_wall_s = compose_wall;
    telemetry_inputs.frame_wall_s = frame_wall;
    telemetry_inputs.pipeline_wall_s = pipeline_wall;
    telemetry_inputs.total_wall_s = seconds_since(total_start);
    telemetry_inputs.stats = stats;
    telemetry_inputs.frame_bytes = frame_bytes;
    telemetry_inputs.output_bytes = output_bytes;
    telemetry_inputs.profile = profile;
    telemetry_inputs.sampler = sampler;
    DynamicMapTelemetry telemetry = build_map_plate_telemetry(telemetry_inputs);

    std::string summary_json;
    if (!telemetry.emit(summary_json, error)) {
        std::remove(staged_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "the native map render cannot certify its telemetry: " + error};
    }

    if (const char* install_error = install_plate_outputs(
            request, summary_json, staged_video, staged_summary)) {
        return {MapPlateStatus::kRenderFailed, install_error};
    }
    return {MapPlateStatus::kOk, ""};
}
}  // namespace chrononplugin::maps
