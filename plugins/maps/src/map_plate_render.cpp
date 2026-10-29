// Chronon3D native map-plate rendering — TICKET-MAPS-NATIVE-PORT.
//
// Native flyover renderer: vector plate sampling (fase 2), camera-aligned
// labels (fase 3), the product "dive" motion (fase 4) and the certified
// chronon.dynamic-map-telemetry.v1 summary (fase 5). Outputs are staged and
// installed only together with a complete telemetry summary; every payload
// outside the native subset is refused, never guessed.

#include "chrononplugin/maps/map_plate_render.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "chrononplugin/maps/map_camera.hpp"
#include "chrononplugin/maps/map_label_render.hpp"
#include "chrononplugin/maps/map_plate_compose.hpp"
#include "chrononplugin/maps/map_plate_raster.hpp"
#include "chrononplugin/maps/map_plate_telemetry.hpp"
#include "chrononplugin/maps/map_route_render.hpp"
#include "chrononplugin/maps/esri_satellite.hpp"
#include "chrononplugin/maps/geojson.hpp"
#include "chrononplugin/maps/map_story_effects.hpp"
#include <cstdint>
#include <cstddef>

namespace chrononplugin::maps {

namespace {

/// The closed encoder profiles the product accepts (mirrors
/// render_dynamic_map_overlay.resolve_encoder_profile, exact tuples only).
struct EncoderProfile {
    std::string codec;
    bool gpu = false;
    std::string preset;
    int quality = 0;
};

bool resolve_encoder_profile(const nlohmann::json& profile, EncoderProfile& out,
                             std::string& error) {
    if (!profile.is_object()) {
        error = "encoder_profile must be an object";
        return false;
    }
    const EncoderProfile given{
        profile.value("codec", ""), profile.value("gpu", false),
        profile.value("preset", ""), profile.value("quality", 0)};
    const bool certified_nvenc = given.codec == "h264_nvenc" && given.gpu &&
        given.preset == "p6" && given.quality == 15;
    const bool certified_fast_x264 = given.codec == "libx264" && !given.gpu &&
        given.preset == "veryfast" && given.quality == 23;
    const bool certified_quality_x264 = given.codec == "libx264" && !given.gpu &&
        given.preset == "slow" && given.quality == 17;
    if (!certified_nvenc && !certified_fast_x264 && !certified_quality_x264) {
        error = "unsupported or incomplete dynamic-map encoder profile";
        return false;
    }
    out = given;
    return true;
}

/// render_dynamic_map_overlay.encoder_arguments (exact argument order).
std::vector<std::string> encoder_arguments(const EncoderProfile& profile) {
    if (profile.gpu) {
        return {"-c:v", profile.codec, "-gpu", "0", "-preset", profile.preset,
                "-tune", "hq", "-rc", "vbr", "-cq", std::to_string(profile.quality),
                "-b:v", "0"};
    }
    std::vector<std::string> args{"-c:v", profile.codec, "-preset", profile.preset,
                                  "-crf", std::to_string(profile.quality)};
    if (profile.preset == "slow" && profile.quality == 17) {
        args.insert(args.end(), {"-maxrate", "16M", "-bufsize", "32M"});
    }
    return args;
}

/// Shell single-quote escaping for the ffmpeg command line.
[[nodiscard]] std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

[[nodiscard]] double seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

struct Pin {
    std::string text;
    double lat = 0.0;
    double lon = 0.0;
};

/// normalize_map_pins: one stop per coordinate (round-4 key), fail-closed
/// above the five-stop route budget.
bool normalize_pins(const nlohmann::json& raw_pins, std::vector<Pin>& pins,
                    std::string& error) {
    if (!raw_pins.is_array()) {
        error = "pins must be an array";
        return false;
    }
    std::set<std::pair<long, long>> seen;
    for (const auto& raw : raw_pins) {
        Pin pin;
        if (!raw.contains("latitude") || !raw.contains("longitude")) {
            error = "every pin requires latitude and longitude";
            return false;
        }
        pin.lat = raw.at("latitude").get<double>();
        pin.lon = raw.at("longitude").get<double>();
        const std::string label = raw.value("label", "");
        pin.text = label.empty() ? raw.value("id", "") : label;
        if (pin.text.empty()) {
            error = "every pin requires a label or id";
            return false;
        }
        const auto key = std::make_pair(
            std::lround(pin.lat * 1e4), std::lround(pin.lon * 1e4));
        if (seen.insert(key).second) {
            pins.push_back(std::move(pin));
        }
    }
    if (pins.empty()) {
        error = "the map plate requires at least one pin";
        return false;
    }
    if (pins.size() > 5) {
        error = "map group exceeds the five-stop route budget; the planner must "
                "split it (the renderer never silently drops a grounded place)";
        return false;
    }
    return true;
}

[[nodiscard]] const char* catalog_dir() {
    if (const char* env = std::getenv("CHRONON_MAPS_CATALOG_DIR")) {
        return env;
    }
#ifdef CHRONON_MAPS_CATALOG_DIR_DEFAULT
    return CHRONON_MAPS_CATALOG_DIR_DEFAULT;
#else
    return "";
#endif
}

/// The payload keys the renderer knows. Keys outside this set are refused:
/// the renderer never ignores a field it does not understand.
///
///  * metadata (map_group, composition_family, ...) are planning provenance
///    and carry no pixel semantics: accepted and recorded by the consumer;
///  * semantic keys must state what the native subset certifies, otherwise
///    the request is refused with kNotImplemented — never approximated.
[[nodiscard]] bool known_payload_keys(const nlohmann::json& payload,
                                      std::string& error) {
    static const std::set<std::string> kKnown = {
        // media contract
        "width", "height", "fps_num", "fps_den", "duration_us",
        // planning provenance (no pixel semantics)
        "plate_id", "map_group", "map_group_strategy", "composition_family",
        "composition_fallback", "typography",
        // semantics, verified below
        "pins", "pin_timing", "encoder_profile", "sampler",
        "camera_animation", "label_animation", "map_animation", "basemap",
        "route_timing", "country_context", "area_glow_radius_km",
        "camera_anchor", "camera_start_zoom", "camera_end_zoom",
        "camera_motion_duration_us",
        "territory_feature", "country_feature", "timeline_label",
        "mountain_overlay_asset",
        "documentary_finish", "route_altitudes_m",
    };
    for (auto it = payload.begin(); it != payload.end(); ++it) {
        if (kKnown.find(it.key()) == kKnown.end()) {
            error = "payload field outside the native map subset: " + it.key();
            return false;
        }
    }
    return true;
}

/// Pin-level fields follow the same rule as the payload: only the grounded
/// place identity is known. Per-pin presentation (accent color, cards,
/// subtitles) belongs to composed presentations that are not ported, so it is
/// refused by name — the renderer never silently ignores a field it does not
/// draw.
[[nodiscard]] bool known_pin_keys(const nlohmann::json& payload,
                                  std::string& error) {
    static const std::set<std::string> kKnownPinKeys = {
        "id", "label", "latitude", "longitude",
    };
    const auto& pins = payload.find("pins");
    if (pins == payload.end()) {
        return true;
    }
    for (const auto& raw : *pins) {
        if (!raw.is_object()) {
            continue;  // shape errors belong to normalize_pins
        }
        for (auto it = raw.begin(); it != raw.end(); ++it) {
            if (kKnownPinKeys.find(it.key()) == kKnownPinKeys.end()) {
                error = "pin field outside the native map subset: " + it.key();
                return false;
            }
        }
    }
    return true;
}

/// Semantic fields the native subset does not certify yet: any non-default
/// value is refused by name (the port adds them one parity gate at a time).
[[nodiscard]] bool supported_semantics(const nlohmann::json& payload,
                                       std::string& error) {
    const std::string map_animation = payload.value("map_animation", "");
    if (supported_map_animations().find(map_animation) ==
        supported_map_animations().end()) {
        error = "map composition '" + map_animation +
                "' is not ported natively yet";
        return false;
    }
    const auto& pin_timing = payload.find("pin_timing");
    if (pin_timing != payload.end() && !pin_timing->is_null() &&
        !pin_timing->empty()) {
        error = "per-pin spoken timing schedules are not ported natively yet";
        return false;
    }
    if (payload.contains("route_timing")) {
        const auto& timing = payload.at("route_timing");
        if (!timing.is_object() || !timing.contains("start_us") ||
            !timing.contains("travel_us") ||
            timing.value("start_us", std::int64_t{-1}) < 0 ||
            timing.value("travel_us", std::int64_t{0}) <= 0) {
            error = "route_timing requires non-negative start_us and positive travel_us";
            return false;
        }
        for (auto it = timing.begin(); it != timing.end(); ++it) {
            if (it.key() != "start_us" && it.key() != "travel_us") {
                error = "unsupported route_timing field: " + it.key();
                return false;
            }
        }
    }
    if (payload.contains("country_context")) {
        error = "country_context map tours are not ported natively yet";
        return false;
    }
    const std::string camera_animation = payload.value("camera_animation", "slow_approach");
    if (supported_map_camera_animations().count(camera_animation) == 0) {
        error = "camera_animation must be slow_approach or exponential_dive";
        return false;
    }
    if (map_animation == "territory_glow_reveal" &&
        (!payload.contains("territory_feature") ||
         !payload.at("territory_feature").is_string())) {
        error = "territory_glow_reveal requires territory_feature";
        return false;
    }
    if (map_animation == "area_glow_pulse" &&
        (!std::isfinite(payload.value("area_glow_radius_km", 0.0)) ||
         payload.value("area_glow_radius_km", 0.0) <= 0.0)) {
        error = "area_glow_pulse requires a positive area_glow_radius_km";
        return false;
    }
    if ((map_animation == "continental_atlas" ||
         map_animation == "historic_location_timeline") &&
        (!payload.contains("country_feature") ||
         !payload.at("country_feature").is_string())) {
        error = map_animation + " requires country_feature";
        return false;
    }
    if (map_animation == "mountain_ridge_callout" &&
        (!payload.contains("mountain_overlay_asset") ||
         !payload.at("mountain_overlay_asset").is_string())) {
        error = "mountain_ridge_callout requires mountain_overlay_asset";
        return false;
    }
    if (map_animation == "historic_location_timeline" &&
        (!payload.contains("timeline_label") ||
         !payload.at("timeline_label").is_string())) {
        error = "historic_location_timeline requires timeline_label";
        return false;
    }
    if ((map_animation == "satellite_scan_reveal" ||
         map_animation == "territory_flood_fill" ||
         map_animation == "territory_extrusion") &&
        (!payload.contains("territory_feature") ||
         !payload.at("territory_feature").is_string())) {
        error = map_animation + " requires territory_feature";
        return false;
    }
    if (map_animation == "city_network_expansion" && payload.contains("pins") &&
        payload.at("pins").size() < 2) {
        error = "city_network_expansion requires at least two pins";
        return false;
    }
    if (payload.contains("documentary_finish") &&
        !payload.at("documentary_finish").is_object()) {
        error = "documentary_finish must be an object";
        return false;
    }
    const std::string basemap = payload.value("basemap", "");
    if (!basemap.empty() && supported_map_basemaps().count(basemap) == 0) {
        error = "basemap '" + basemap + "' is not available natively: the "
                "native renderer supports Esri World Imagery and chronon_editorial";
        return false;
    }
    return true;
}

}  // namespace

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
    std::vector<Pin> pins;
    if (!normalize_pins(payload.value("pins", nlohmann::json::array()), pins, error)) {
        return {MapPlateStatus::kInvalidRequest, error};
    }
    if ((map_animation == "route_line" || map_animation == "multi_leg_flight") &&
        pins.size() < 2) {
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
    std::pair<double, double> anchor{pins.front().lat, pins.front().lon};
    if (payload.contains("camera_anchor")) {
        const auto& a = payload.at("camera_anchor");
        if (!a.is_array() || a.size() != 2) {
            return {MapPlateStatus::kInvalidRequest,
                    "camera_anchor must be a [lat, lon] pair"};
        }
        anchor = {a.at(0).get<double>(), a.at(1).get<double>()};
    } else if (pins.size() > 1) {
        return {MapPlateStatus::kNotImplemented,
                "multi-pin map tours are not ported natively yet; provide an "
                "explicit camera_anchor or a single pin"};
    }
    const double start_zoom = payload.value("camera_start_zoom", 5.2);
    const double end_zoom = payload.value("camera_end_zoom", 17.8);
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

    // Boundary sources for the vector plates.
    const std::string base = catalog_dir();
    if (base[0] == '\0') {
        return {MapPlateStatus::kRenderFailed,
                "no map boundary catalog configured (CHRONON_MAPS_CATALOG_DIR)"};
    }
    const std::string countries = base + "/ne_50m_admin_0_countries.geojson";
    const std::string admin2 =
        base + "/italy_admin/geoBoundaries_ITA_ADM2.simplified.geojson";
    std::vector<GeoPolygon> territory_polygons;
    GeoFeatureCollection country_features;
    cv::Mat mountain_overlay;

    const auto total_start = std::chrono::steady_clock::now();
    const auto prepare_start = total_start;
    std::unique_ptr<VectorPlateRaster> raster;
    std::unique_ptr<EsriSatellitePyramid> satellite;
    try {
        if (map_animation == "mountain_ridge_callout") {
            const std::string overlay_path =
                payload.at("mountain_overlay_asset").get<std::string>();
            mountain_overlay = cv::imread(overlay_path, cv::IMREAD_UNCHANGED);
            if (mountain_overlay.empty() || mountain_overlay.channels() != 4) {
                throw std::runtime_error(
                    "mountain_overlay_asset must be a readable RGBA image: " +
                    overlay_path);
            }
        }
        const bool style_morph = map_animation == "map_style_morph";
        const std::string basemap = payload.value("basemap", "chronon_editorial");
        if (basemap == "esri_sat" || style_morph) {
            satellite = std::make_unique<EsriSatellitePyramid>();
            satellite->prefetch_trajectory(anchor.first, anchor.second,
                frame_zooms,
                request.output_width, request.output_height);
        }
        if (basemap != "esri_sat" || style_morph) {
            raster = std::make_unique<VectorPlateRaster>(VectorMapSources{countries, admin2});
        }
        if (map_animation == "territory_glow_reveal" ||
            map_animation == "satellite_scan_reveal" ||
            map_animation == "territory_flood_fill" ||
            map_animation == "territory_extrusion") {
            const std::string path = base + "/italy_admin/geoBoundaries_ITA_ADM3.simplified.geojson";
            std::ifstream input(path);
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
            map_animation == "historic_location_timeline") {
            std::ifstream input(countries);
            if (!input) throw std::runtime_error("cannot open country GeoJSON: " + countries);
            const std::string json((std::istreambuf_iterator<char>(input)), {});
            const auto collection = parse_geojson(json);
            const std::string target_name = payload.at("country_feature").get<std::string>();
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

    std::string ffmpeg_cmd = "/usr/bin/ffmpeg -hide_banner -loglevel error -y";
    ffmpeg_cmd += " -f rawvideo -pix_fmt bgr24 -video_size " +
                  std::to_string(request.output_width) + "x" +
                  std::to_string(request.output_height);
    ffmpeg_cmd += " -framerate " + std::to_string(fps_num) + "/" +
                  std::to_string(fps_den);
    ffmpeg_cmd += " -i -";
    for (const std::string& arg : encoder_arguments(profile)) {
        ffmpeg_cmd += " " + arg;
    }
    ffmpeg_cmd += " -pix_fmt yuv420p -movflags +faststart " + shell_quote(staged_video);

    std::FILE* encoder = ::popen(ffmpeg_cmd.c_str(), "w");
    if (encoder == nullptr) {
        return {MapPlateStatus::kRenderFailed,
                "cannot launch the ffmpeg encoder for the map plate"};
    }

    PlateComposeStats stats;
    double compose_wall = 0.0;
    double frame_wall = 0.0;
    bool io_error = false;
    const std::size_t frame_bytes = static_cast<std::size_t>(request.output_width) *
                                    request.output_height * 3;
    std::vector<unsigned char> buffer(frame_bytes);

    const auto pipeline_start = std::chrono::steady_clock::now();
    for (std::int64_t f = 0; f < request.frame_count && !io_error; ++f) {
        const auto frame_start = std::chrono::steady_clock::now();
        const double zoom = frame_zooms[static_cast<std::size_t>(f)];
        const double linear_progress = static_cast<double>(f) /
            static_cast<double>(std::max<std::int64_t>(1, request.frame_count - 1));
        const double story_progress = std::clamp((linear_progress - 0.08) / 0.30, 0.0, 1.0);
        const auto compose_start = std::chrono::steady_clock::now();
        cv::Mat frame;
        if (map_animation == "map_style_morph") {
            cv::Mat satellite_frame = satellite->sample(anchor.first, anchor.second,
                zoom, request.output_width, request.output_height);
            cv::Mat editorial_frame = compose_sample_continuous(*raster,
                anchor.first, anchor.second, zoom, request.output_width,
                request.output_height, &stats);
            cv::Mat editorial_gray;
            cv::cvtColor(editorial_frame, editorial_gray, cv::COLOR_BGR2GRAY);
            cv::bitwise_not(editorial_gray, editorial_gray);
            cv::cvtColor(editorial_gray, editorial_frame, cv::COLOR_GRAY2BGR);
            editorial_frame.convertTo(editorial_frame, -1, 0.82, 62.0);
            const double transition = std::clamp((linear_progress - 0.06) / 0.30, 0.0, 1.0);
            cv::addWeighted(satellite_frame, 1.0 - transition * 0.48,
                            editorial_frame, transition * 0.48, 0.0, frame);
        } else {
            frame = satellite
                ? satellite->sample(anchor.first, anchor.second, zoom,
                                    request.output_width, request.output_height)
                : compose_sample_continuous(*raster, anchor.first, anchor.second,
                                            zoom, request.output_width,
                                            request.output_height, &stats);
        }
        compose_wall += seconds_since(compose_start);
        if (camera_animation == "exponential_dive" && f > 0) {
            apply_radial_motion_blur(frame,
                frame_zooms[static_cast<std::size_t>(f)] -
                frame_zooms[static_cast<std::size_t>(f - 1)]);
        }
        if (map_animation == "route_line" || map_animation == "multi_leg_flight") {
            std::vector<MapRouteStop> stops;
            stops.reserve(pins.size());
            for (const Pin& pin : pins) {
                stops.push_back({pin.text, pin.lat, pin.lon});
            }
            double progress = linear_progress;
            if (payload.contains("route_timing")) {
                const auto& timing = payload.at("route_timing");
                const double time_us = static_cast<double>(f) * 1e6 * fps_den / fps_num;
                progress = (time_us - timing.at("start_us").get<double>()) /
                           timing.at("travel_us").get<double>();
            }
            draw_animated_route(frame, stops, anchor, zoom, progress,
                                static_cast<int>(f), route_altitudes);
        } else if (map_animation == "territory_glow_reveal") {
            const double time_us = static_cast<double>(f) * 1e6 * fps_den / fps_num;
            const double reveal_start_us = static_cast<double>(motion_duration_us);
            const double reveal_duration_us = std::min(1'600'000.0,
                std::max(1.0, static_cast<double>(duration_us) - reveal_start_us));
            const double reveal_progress = std::clamp(
                (time_us - reveal_start_us) / reveal_duration_us, 0.0, 1.0);
            draw_territory_wipe(frame, territory_polygons, anchor, zoom,
                reveal_progress * reveal_progress * (3.0 - 2.0 * reveal_progress));
            const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
                anchor, zoom, request.output_width, request.output_height);
            const double title_fade_start_us = std::max(0.0,
                static_cast<double>(duration_us) - 1'000'000.0);
            const double title_opacity = std::clamp(
                (time_us - title_fade_start_us) / 500'000.0, 0.0, 1.0);
            draw_territory_callout(frame, {x, y},
                payload.at("territory_feature").get<std::string>(), title_opacity);
        } else if (map_animation == "area_glow_pulse") {
            const double latitude = pins.front().lat;
            const double metres_per_pixel = 156543.03392 *
                std::cos(latitude * 3.14159265358979323846 / 180.0) /
                std::pow(2.0, zoom);
            const double radius_pixels = payload.value("area_glow_radius_km", 0.0) *
                1000.0 / std::max(1e-9, metres_per_pixel);
            const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
                anchor, zoom, request.output_width, request.output_height);
            draw_area_glow(frame, {x, y}, radius_pixels, linear_progress,
                           pins.front().text);
        } else if (map_animation == "radar_location_reveal") {
            const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
                anchor, zoom, request.output_width, request.output_height);
            draw_radar_reveal(frame, {x, y}, story_progress, pins.front().text);
        } else if (map_animation == "satellite_scan_reveal") {
            draw_satellite_scan(frame, territory_polygons, anchor, zoom,
                story_progress, pins.front().text);
        } else if (map_animation == "territory_flood_fill") {
            draw_territory_flood_fill(frame, territory_polygons, anchor, zoom,
                story_progress, pins.front().text);
        } else if (map_animation == "light_beam_drop") {
            const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
                anchor, zoom, request.output_width, request.output_height);
            draw_light_beam_drop(frame, {x, y}, story_progress, pins.front().text);
        } else if (map_animation == "city_network_expansion") {
            std::vector<cv::Point> nodes;
            std::vector<std::string> labels;
            nodes.reserve(pins.size());
            labels.reserve(pins.size());
            for (const Pin& pin : pins) {
                const auto [x, y] = geo_to_screen(pin.lat, pin.lon, anchor, zoom,
                    request.output_width, request.output_height);
                nodes.emplace_back(x, y);
                labels.push_back(pin.text);
            }
            draw_city_network(frame, nodes, labels, story_progress);
        } else if (map_animation == "territory_extrusion") {
            draw_territory_extrusion(frame, territory_polygons, anchor, zoom,
                story_progress, pins.front().text);
        } else if (map_animation == "map_style_morph") {
            const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
                anchor, zoom, request.output_width, request.output_height);
            draw_location_callout(frame, {x, y}, pins.front().text,
                std::clamp((linear_progress - 0.18) * 4.0, 0.0, 1.0));
        } else if (map_animation == "mountain_ridge_callout") {
            draw_mountain_ridge_callout(frame, mountain_overlay, story_progress,
                                        pins.front().text);
        } else if (map_animation == "continental_atlas") {
            std::vector<cv::Point> label_points;
            std::vector<std::string> labels;
            for (const Pin& pin : pins) {
                const auto [x, y] = geo_to_screen(pin.lat, pin.lon, anchor, zoom,
                    request.output_width, request.output_height);
                label_points.emplace_back(x, y);
                labels.push_back(pin.text);
            }
            draw_continental_atlas(frame, country_features, anchor, zoom,
                story_progress, payload.at("country_feature").get<std::string>(),
                label_points, labels);
        } else if (map_animation == "historic_location_timeline") {
            const auto [x, y] = geo_to_screen(pins.front().lat, pins.front().lon,
                anchor, zoom, request.output_width, request.output_height);
            draw_historic_location_timeline(frame, territory_polygons, anchor, zoom,
                story_progress, {x, y}, pins.front().text,
                payload.at("timeline_label").get<std::string>());
        } else {
            for (const Pin& pin : pins) {
                const auto [x, y] = geo_to_screen(pin.lat, pin.lon, anchor, zoom,
                                                  request.output_width,
                                                  request.output_height);
                draw_camera_aligned_label(frame, {static_cast<double>(x),
                                                  static_cast<double>(y)},
                                          pin.text, {255, 255, 255});
            }
        }
        if (satellite) {
            const std::string credit = "Sources: Esri, Maxar, Earthstar Geographics, and the GIS User Community";
            const double scale = std::max(0.25, std::min(0.36, request.output_width / 1920.0 * 0.34));
            int baseline = 0;
            const cv::Size extent = cv::getTextSize(credit, cv::FONT_HERSHEY_SIMPLEX,
                                                     scale, 1, &baseline);
            const cv::Point origin(std::max(8, request.output_width - extent.width - 12),
                                   request.output_height - 10);
            cv::putText(frame, credit, origin, cv::FONT_HERSHEY_SIMPLEX, scale,
                        cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
            cv::putText(frame, credit, origin, cv::FONT_HERSHEY_SIMPLEX, scale,
                        cv::Scalar(248, 246, 236), 1, cv::LINE_AA);
        }
        apply_documentary_finish(frame, static_cast<int>(f), vignette_strength,
                                 grain_strength);
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
        if (std::fwrite(frame.data, 1, frame_bytes, encoder) != frame_bytes) {
            io_error = true;
        }
        frame_wall += seconds_since(frame_start);
    }
    const double pipeline_wall = seconds_since(pipeline_start);

    const int encoder_status = ::pclose(encoder);
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
    // Certified telemetry — distinguish downloaded/cached imagery tiles from
    // locally rasterized editorial tiles.
    DynamicMapTelemetry telemetry;
    telemetry.basemap = satellite ? "esri_sat" : "chronon_editorial";
    telemetry.frames = request.frame_count;
    telemetry.width = request.output_width;
    telemetry.height = request.output_height;
    telemetry.fps_num = fps_num;
    telemetry.fps_den = fps_den;
    telemetry.renderer_wall_s = seconds_since(total_start);
    telemetry.frame_pipeline_s = frame_wall > 0.0 ? frame_wall : pipeline_wall;
    telemetry.render_encode_wall_s = pipeline_wall;
    telemetry.post_frame_tail_s = 0.0;
    telemetry.gate_ms = 0.0;
    telemetry.output_bytes = output_bytes;
    telemetry.video_encoder = profile.codec;
    telemetry.gpu_encoder = profile.gpu;
    telemetry.gpu_map_sampler = sampler;
    telemetry.encoder_preset = profile.preset;
    telemetry.encoder_quality = profile.quality;
    telemetry.tile.disk_cache_hits = satellite ? satellite->disk_hits() : 0;
    telemetry.tile.disk_cache_misses = satellite ? satellite->network_fetches() : stats.tile_renders;
    telemetry.tile.network_fetches = satellite ? satellite->network_fetches() : 0;
    telemetry.tile.bytes_downloaded = satellite ? satellite->bytes_downloaded() : 0;
    telemetry.tile.fallbacks = 0;
    telemetry.tile.rendered_locally = satellite ? 0 : stats.tile_renders;
    telemetry.tile.fetch_ms = satellite ? satellite->prefetch_ms() : compose_wall * 1e3;
    telemetry.tile.prefetch_ms = satellite ? satellite->prefetch_ms() : 0.0;
    const std::int64_t tile_count = satellite
        ? satellite->disk_hits() + satellite->network_fetches() : stats.tile_renders;
    const std::int64_t tile_cache_hits = satellite ? satellite->disk_hits() : 0;
    const std::int64_t tile_fetched = satellite ? satellite->network_fetches() : stats.tile_renders;
    telemetry.tile.prefetch_tiles_requested = tile_count;
    telemetry.tile.prefetch_tiles_memory_hits = tile_cache_hits;
    telemetry.plates.prepare_ms = prepare_ms;
    telemetry.plates.compose_ms = compose_wall * 1e3;
    telemetry.plates.plate_count = satellite ? satellite->plate_count() : stats.plate_composes;
    telemetry.plates.plate_bytes = satellite ? satellite->plate_bytes() : stats.plate_bytes;
    telemetry.plates.required_tile_count = tile_count;
    telemetry.plates.prefetch_tiles_fetched = tile_fetched;
    telemetry.plates.prefetch_tiles_memory_hits = tile_cache_hits;

    std::string summary_json;
    if (!telemetry.emit(summary_json, error)) {
        std::remove(staged_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "the native map render cannot certify its telemetry: " + error};
    }

    // Install the outputs together, only now that both exist.
    std::FILE* summary_file = std::fopen(staged_summary.c_str(), "wb");
    if (summary_file == nullptr ||
        std::fwrite(summary_json.data(), 1, summary_json.size(), summary_file) !=
            summary_json.size()) {
        if (summary_file != nullptr) std::fclose(summary_file);
        std::remove(staged_video.c_str());
        std::remove(staged_summary.c_str());
        return {MapPlateStatus::kRenderFailed,
                "cannot write the map plate telemetry summary"};
    }
    std::fclose(summary_file);
    if (std::rename(staged_video.c_str(), request.output_video.c_str()) != 0 ||
        std::rename(staged_summary.c_str(), request.summary_output.c_str()) != 0) {
        std::remove(staged_video.c_str());
        std::remove(staged_summary.c_str());
        std::remove(request.output_video.c_str());
        return {MapPlateStatus::kRenderFailed,
                "cannot install the map plate outputs"};
    }
    return {MapPlateStatus::kOk, ""};
}

}  // namespace chrononplugin::maps
