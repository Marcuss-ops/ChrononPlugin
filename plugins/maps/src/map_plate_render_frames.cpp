// Per-frame compositor of the native map renderer. Split out of
// map_plate_render_pipeline.cpp: base-plate sampling, the map-animation
// dispatch, the named-place fill, the imagery credits and the documentary
// finish. The driver keeps only scheduling, pipe I/O and telemetry.

#include "map_plate_render_pipeline_internal.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_label_render.hpp"
#include "chrononplugin/maps/map_route_render.hpp"

namespace chrononplugin::maps::map_plate_detail {

cv::Mat render_plate_frame(const MapPlateScene& scene, std::int64_t frame_index,
                           double zoom, PlateComposeStats& stats,
                           FrameTimings& timings,
                           const cv::Mat* cached_plate,
                           cv::Mat* plate_out) {
    const auto& payload = *scene.payload;
    const std::int64_t f = frame_index;
    const double linear_progress = static_cast<double>(f) /
        static_cast<double>(std::max<std::int64_t>(1, scene.frames - 1));
    // A named-place render is a map-style preview. Keep its geographic
    // treatment in the completed pose for the whole clip; only the camera
    // performs the requested gentle push-in.
    const double story_progress = scene.map_place.empty()
        ? std::clamp((linear_progress - 0.08) / 0.30, 0.0, 1.0)
        : 1.0;
    const auto compose_start = std::chrono::steady_clock::now();
    cv::Mat frame;
    // Globe dive paints the complete frame until it starts revealing the live
    // map. Do not build a base plate whose pixels would be discarded.
    const bool globe_pre_reveal = scene.map_animation == "globe_dive" &&
        linear_progress < 0.665;
    if (globe_pre_reveal) {
        frame = cv::Mat(scene.output_height, scene.output_width, CV_8UC3,
                        cv::Scalar(0, 0, 0));
    } else if (cached_plate != nullptr && !cached_plate->empty()) {
        // Same camera pose as the previous frame: the base plate is identical,
        // so only the per-frame treatment below has to run.
        frame = cached_plate->clone();
    } else if (scene.self_contained_map) {
        frame = cv::Mat(scene.output_height, scene.output_width,
                        CV_8UC3, cv::Scalar(0, 0, 0));
    } else if (scene.map_animation == "map_style_morph") {
        cv::Mat satellite_frame = scene.satellite->sample(
            scene.anchor.first, scene.anchor.second, zoom, scene.output_width,
            scene.output_height);
        cv::Mat editorial_frame = compose_sample_continuous(*scene.raster,
            scene.anchor.first, scene.anchor.second, zoom, scene.output_width,
            scene.output_height, &stats);
        cv::Mat editorial_gray;
        cv::cvtColor(editorial_frame, editorial_gray, cv::COLOR_BGR2GRAY);
        cv::bitwise_not(editorial_gray, editorial_gray);
        cv::cvtColor(editorial_gray, editorial_frame, cv::COLOR_GRAY2BGR);
        editorial_frame.convertTo(editorial_frame, -1, 0.82, 62.0);
        const double transition = scene.map_place.empty()
            ? std::clamp((linear_progress - 0.06) / 0.30, 0.0, 1.0)
            : 1.0;
        cv::addWeighted(satellite_frame, 1.0 - transition * 0.48,
                        editorial_frame, transition * 0.48, 0.0, frame);
    } else {
        frame = scene.satellite
            ? scene.satellite->sample(scene.anchor.first, scene.anchor.second,
                                      zoom, scene.output_width,
                                      scene.output_height)
            : compose_sample_continuous(*scene.raster, scene.anchor.first,
                                        scene.anchor.second, zoom,
                                        scene.output_width, scene.output_height,
                                        &stats);
        // Hand the pristine plate to the driver before any effect draws on it.
        if (plate_out != nullptr) *plate_out = frame.clone();
    }
    timings.compose_s = seconds_since(compose_start);
    if (!scene.self_contained_map &&
        scene.camera_animation == "exponential_dive" && f > 0) {
        apply_radial_motion_blur(frame,
            zoom - scene.previous_zoom);
    }
    const auto effect_start = std::chrono::steady_clock::now();
    const std::vector<Pin>& pins = scene.pins;
    if ((scene.map_animation == "route_line" ||
         scene.map_animation == "multi_leg_flight") && pins.size() >= 2) {
        std::vector<MapRouteStop> stops;
        stops.reserve(pins.size());
        for (const Pin& pin : pins) {
            stops.push_back({pin.text, pin.lat, pin.lon});
        }
        double progress = linear_progress;
        if (payload.contains("route_timing")) {
            const auto& timing = payload.at("route_timing");
            const double time_us = static_cast<double>(f) * 1e6 * scene.fps_den /
                                   scene.fps_num;
            progress = (time_us - timing.at("start_us").get<double>()) /
                       timing.at("travel_us").get<double>();
        }
        draw_animated_route(frame, stops, scene.anchor, zoom, progress,
                            static_cast<int>(f), scene.route_altitudes);
    } else if (scene.map_animation == "territory_glow_reveal") {
        const double time_us = static_cast<double>(f) * 1e6 * scene.fps_den /
                               scene.fps_num;
        const double reveal_start_us = static_cast<double>(scene.motion_duration_us);
        const double reveal_duration_us = std::min(1'600'000.0,
            std::max(1.0, static_cast<double>(scene.duration_us) - reveal_start_us));
        const double reveal_progress = std::clamp(
            (time_us - reveal_start_us) / reveal_duration_us, 0.0, 1.0);
        draw_territory_wipe(frame, scene.territory_polygons, scene.anchor, zoom,
            reveal_progress * reveal_progress * (3.0 - 2.0 * reveal_progress));
        const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
            scene.anchor, zoom, scene.output_width, scene.output_height);
        const double title_fade_start_us = std::max(0.0,
            static_cast<double>(scene.duration_us) - 1'000'000.0);
        const double title_opacity = std::clamp(
            (time_us - title_fade_start_us) / 500'000.0, 0.0, 1.0);
        draw_territory_callout(frame, {x, y},
            pins.front().text, title_opacity);
    } else if (scene.map_animation == "area_glow_pulse") {
        const double latitude = pins.front().lat;
        const double metres_per_pixel = 156543.03392 *
            std::cos(latitude * 3.14159265358979323846 / 180.0) /
            std::pow(2.0, zoom);
        const double radius_km = payload.value("area_glow_radius_km",
            scene.map_place.empty() ? 0.0 : 100.0);
        const double radius_pixels = radius_km *
            1000.0 / std::max(1e-9, metres_per_pixel);
        const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
            scene.anchor, zoom, scene.output_width, scene.output_height);
        const double pulse_phase =
            area_glow_pulse_phase(linear_progress, scene.duration_us);
        draw_area_glow(frame, {x, y}, radius_pixels, pulse_phase,
                       pins.front().text);
    } else if (scene.map_animation == "radar_location_reveal") {
        const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
            scene.anchor, zoom, scene.output_width, scene.output_height);
        draw_radar_reveal(frame, {x, y}, story_progress, pins.front().text);
    } else if (scene.map_animation == "satellite_scan_reveal") {
        draw_satellite_scan(frame, scene.territory_polygons, scene.anchor, zoom,
            story_progress, pins.front().text);
    } else if (scene.map_animation == "territory_flood_fill") {
        draw_territory_flood_fill(frame, scene.territory_polygons, scene.anchor,
            zoom, story_progress,
            scene.show_text && payload.value("show_location_tag", true)
                ? pins.front().text : "");
    } else if (scene.map_animation == "territory_tactical_fill") {
        draw_tactical_territory(frame, scene.territory_polygons, scene.anchor,
            zoom, story_progress, pins.front().text,
            payload.value("territory_subtitle", ""),
            scene.profiling ? scene.tactical_timings : nullptr);
    } else if (scene.map_animation == "volumetric_laser_scan") {
        draw_volumetric_laser_scan(frame, scene.territory_polygons,
            scene.anchor, zoom, story_progress, pins.front().text,
            scene.show_text);
    } else if (scene.map_animation == "vector_particle_flow") {
        draw_vector_particle_flow(frame, scene.territory_polygons, scene.anchor,
            zoom, story_progress, pins.front().text, scene.show_text);
    } else if (scene.map_animation == "dark_mirror_diorama") {
        draw_dark_mirror_diorama(frame, scene.territory_polygons, scene.anchor,
            zoom, story_progress, pins.front().text, scene.show_text);
    } else if (scene.map_animation == "voronoi_heat_contours") {
        draw_voronoi_heat_contours(frame, scene.territory_polygons,
            scene.anchor, zoom, story_progress, pins.front().text,
            scene.show_text);
    } else if (scene.map_animation == "holographic_postfx") {
        draw_holographic_postfx(frame, scene.territory_polygons, scene.anchor,
            zoom, story_progress, pins.front().text, scene.show_text);
    } else if (scene.map_animation == "light_beam_drop") {
        const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
            scene.anchor, zoom, scene.output_width, scene.output_height);
        draw_light_beam_drop(frame, {x, y}, story_progress, pins.front().text);
    } else if (scene.map_animation == "city_network_expansion") {
        std::vector<cv::Point> nodes;
        std::vector<std::string> labels;
        nodes.reserve(pins.size());
        labels.reserve(pins.size());
        for (const Pin& pin : pins) {
            const auto [x, y] = geo_to_screen(pin.lat, pin.lon, scene.anchor,
                zoom, scene.output_width, scene.output_height);
            nodes.emplace_back(x, y);
            labels.push_back(pin.text);
        }
        draw_city_network(frame, nodes, labels, story_progress);
    } else if (scene.map_animation == "territory_extrusion") {
        draw_territory_extrusion(frame, scene.territory_polygons, scene.anchor,
            zoom, story_progress, pins.front().text);
    } else if (scene.map_animation == "map_style_morph") {
        const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
            scene.anchor, zoom, scene.output_width, scene.output_height);
        draw_location_callout(frame, {x, y}, pins.front().text,
            std::clamp((linear_progress - 0.18) * 4.0, 0.0, 1.0));
    } else if (scene.map_animation == "mountain_ridge_callout") {
        if (!scene.mountain_overlay.empty()) {
            draw_mountain_ridge_callout(frame, scene.mountain_overlay,
                                        story_progress, pins.front().text,
                                        scene.show_text);
        }
    } else if (scene.map_animation == "continental_atlas") {
        std::vector<cv::Point> label_points;
        std::vector<std::string> labels;
        if (scene.show_text) {
            for (const Pin& pin : pins) {
                const auto [x, y] = geo_to_screen(pin.lat, pin.lon, scene.anchor,
                    zoom, scene.output_width, scene.output_height);
                label_points.emplace_back(x, y);
                labels.push_back(pin.text);
            }
        }
        draw_continental_atlas(frame, scene.country_features, scene.anchor,
            zoom, story_progress, payload.value("country_feature", scene.map_place),
            label_points, labels);
    } else if (scene.map_animation == "historic_location_timeline") {
        const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
            scene.anchor, zoom, scene.output_width, scene.output_height);
        draw_historic_location_timeline(frame, scene.territory_polygons,
            scene.anchor, zoom, story_progress, {x, y}, pins.front().text,
            scene.show_text ? payload.value("timeline_label", "") : "",
            scene.show_text);
    } else if (scene.map_animation == "country_spotlight_reveal") {
        const double reveal_progress = scene.map_place.empty()
            ? story_progress
            : std::clamp((linear_progress - 0.08) / 0.30, 0.0, 1.0);
        draw_country_spotlight(frame, scene.territory_polygons, scene.anchor,
            zoom, reveal_progress, pins.front().text, scene.show_text);
    } else if (scene.map_animation == "data_bars_reveal") {
        DataMapSpec spec;
        const auto& data = payload.value("data", nlohmann::json::object());
        spec.title = data.value("title", "");
        spec.prefix = data.value("prefix", "");
        spec.unit = data.value("unit", "");
        spec.scheme = data.value("scheme", "heat");
        spec.labels = data.value("labels", "both");
        spec.show_legend = data.value("legend", true);
        spec.show_text = scene.show_text;
        spec.has_min = data.contains("min");
        spec.has_max = data.contains("max");
        if (spec.has_min) spec.min_value = data.at("min").get<double>();
        if (spec.has_max) spec.max_value = data.at("max").get<double>();
        const auto& values = data.at("values");
        for (auto it = values.begin(); it != values.end(); ++it) {
            spec.values.push_back({it.key(), it.value().get<double>()});
        }
        draw_data_bars(frame, spec, story_progress, scene.show_text);
    } else if (scene.map_animation == "label_choreography") {
        std::vector<cv::Point> anchors;
        std::vector<std::string> labels;
        anchors.reserve(pins.size());
        labels.reserve(pins.size());
        for (const Pin& pin : pins) {
            const auto [x, y] = geo_to_screen(pin.lat, pin.lon, scene.anchor,
                zoom, scene.output_width, scene.output_height);
            anchors.emplace_back(x, y);
            labels.push_back(pin.text);
        }
        draw_label_choreography(frame, anchors, labels, story_progress,
                                scene.show_text);
    } else if (scene.map_animation == "paper_map_fold") {
        draw_paper_map_fold(frame, linear_progress, pins.front().text,
                            scene.show_text);
    } else if (scene.map_animation == "globe_dive") {
        draw_globe_dive(frame, scene.country_features, linear_progress,
                        pins.front().text, scene.show_text);
    } else if (scene.map_animation == "data_choropleth_reveal") {
        DataMapSpec spec;
        const auto& data = payload.value("data", nlohmann::json::object());
        spec.title = data.value("title", "");
        spec.prefix = data.value("prefix", "");
        spec.unit = data.value("unit", "");
        spec.scheme = data.value("scheme", "heat");
        spec.labels = data.value("labels", "both");
        spec.show_legend = data.value("legend", true);
        spec.show_text = scene.show_text;
        spec.has_min = data.contains("min");
        spec.has_max = data.contains("max");
        if (spec.has_min) spec.min_value = data.at("min").get<double>();
        if (spec.has_max) spec.max_value = data.at("max").get<double>();
        const auto& values = data.at("values");
        for (auto it = values.begin(); it != values.end(); ++it) {
            spec.values.push_back({it.key(), it.value().get<double>()});
        }
        draw_data_choropleth(frame, scene.country_features, scene.anchor, zoom,
                             story_progress, spec);
    } else {
        for (const Pin& pin : pins) {
            const auto [x, y] = geo_to_screen(pin.lat, pin.lon, scene.anchor,
                                              zoom, scene.output_width,
                                              scene.output_height);
            draw_camera_aligned_label(frame, {static_cast<double>(x),
                                              static_cast<double>(y)},
                                      pin.text, {255, 255, 255});
        }
    }
    const bool animation_fills_place =
        scene.map_animation == "territory_glow_reveal" ||
        scene.map_animation == "satellite_scan_reveal" ||
        scene.map_animation == "territory_flood_fill" ||
        scene.map_animation == "territory_tactical_fill" ||
        scene.map_animation == "territory_extrusion" ||
        scene.map_animation == "volumetric_laser_scan" ||
        scene.map_animation == "vector_particle_flow" ||
        scene.map_animation == "dark_mirror_diorama" ||
        scene.map_animation == "voronoi_heat_contours" ||
        scene.map_animation == "holographic_postfx" ||
        scene.map_animation == "continental_atlas" ||
        scene.map_animation == "data_choropleth_reveal" ||
        scene.map_animation == "country_spotlight_reveal";
    if (!scene.map_place.empty() && !animation_fills_place) {
        const double place_fill_progress = std::clamp(
            linear_progress / 0.24, 0.0, 1.0);
        const double eased_fill = place_fill_progress * place_fill_progress *
                                  (3.0 - 2.0 * place_fill_progress);
        draw_named_place_fill(frame, scene.named_place_polygons, scene.anchor,
                              zoom, eased_fill);
    }
    timings.effect_s = seconds_since(effect_start);
    if (scene.satellite) {
        const std::string credit = "Sources: Esri, Maxar, Earthstar Geographics, and the GIS User Community";
        const double scale = std::max(0.25, std::min(0.36, scene.output_width / 1920.0 * 0.34));
        int baseline = 0;
        const cv::Size extent = cv::getTextSize(credit, cv::FONT_HERSHEY_SIMPLEX,
                                                 scale, 1, &baseline);
        const cv::Point origin(std::max(8, scene.output_width - extent.width - 12),
                               scene.output_height - 10);
        cv::putText(frame, credit, origin, cv::FONT_HERSHEY_SIMPLEX, scale,
                    cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
        cv::putText(frame, credit, origin, cv::FONT_HERSHEY_SIMPLEX, scale,
                    cv::Scalar(248, 246, 236), 1, cv::LINE_AA);
    }
    const auto finish_start = std::chrono::steady_clock::now();
    apply_documentary_finish(frame, static_cast<int>(f), scene.vignette_strength,
                             scene.grain_strength);
    timings.finish_s = seconds_since(finish_start);
    return frame;
}

}  // namespace chrononplugin::maps::map_plate_detail
